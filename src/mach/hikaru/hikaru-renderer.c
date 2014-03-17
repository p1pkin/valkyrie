/* 
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 * 
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

/* XXX implement mesh caching. requires storing color/texhead/vertices. */

#include "vk/input.h"

#include "mach/hikaru/hikaru-renderer.h"
#include "mach/hikaru/hikaru-renderer-private.h"

#define MAX_VIEWPORTS	4096
#define MAX_MODELVIEWS	4096
#define MAX_MATERIALS	4096
#define MAX_TEXHEADS	4096
#define MAX_LIGHTSETS	256
#define MAX_MESHES	16384

#define INV255	(1.0f / 255.0f)

static const struct {
	int32_t min, max;
	uint32_t key;
	bool print;
	char name[32];
} debug_controls[] = {
	[HR_DEBUG_LOG]			= {  0, 1,     ~0, false, "LOG" },
	[HR_DEBUG_NO_LAYER1]		= {  0, 1, SDLK_1, false, "NO LAYER1" },
	[HR_DEBUG_NO_LAYER2]		= {  0, 1, SDLK_2, false, "NO LAYER2" },
	[HR_DEBUG_NO_3D]		= {  0, 1, SDLK_3, false, "NO 3D" },
	[HR_DEBUG_SELECT_BASE_COLOR]	= {  0, 9, SDLK_c,  true, "SELECT BASE COLOR" },
	[HR_DEBUG_SELECT_CULLFACE]	= { -1, 1, SDLK_f,  true, "SELECT CULLFACE" },
	[HR_DEBUG_NO_TEXTURES]		= {  0, 1, SDLK_t, false, "NO TEXTURES" },
	[HR_DEBUG_USE_DEBUG_TEXTURE]	= {  0, 1, SDLK_y, false, "USE DEBUG TEXTURE" },
	[HR_DEBUG_DUMP_TEXTURES]	= {  0, 1,     ~0, false, "DUMP TEXTURES" },
	[HR_DEBUG_DETWIDDLE_TEXTURES]	= {  0, 1, SDLK_u, false, "DETWIDDLE TEXTURES" },
	[HR_DEBUG_SELECT_POLYTYPE]	= { -1, 7, SDLK_p,  true, "SELECT POLYTYPE" },
	[HR_DEBUG_NO_INSTANCING]	= {  0, 1, SDLK_i, false, "NO INSTANCING" },
	[HR_DEBUG_DRAW_NORMALS]		= {  0, 1, SDLK_n, false, "DRAW NORMALS" },
	[HR_DEBUG_NO_LIGHTING]		= {  0, 1, SDLK_l, false, "NO LIGHTING" },
	[HR_DEBUG_NO_AMBIENT]		= {  0, 1, SDLK_a, false, "NO AMBIENT" },
	[HR_DEBUG_NO_DIFFUSE]		= {  0, 1, SDLK_d, false, "NO DIFFUSE" },
	[HR_DEBUG_NO_SPECULAR]		= {  0, 1, SDLK_s, false, "NO SPECULAR" },
	[HR_DEBUG_SELECT_ATT_TYPE]	= { -1, 4, SDLK_z,  true, "SELECT ATT TYPE" },
};

static void
init_debug_flags (hikaru_renderer_t *hr)
{
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++)
		hr->debug.flags[i] = debug_controls[i].min;

	hr->debug.flags[HR_DEBUG_LOG] =
		vk_util_get_bool_option ("HR_LOG", false) ? 1 : 0;
	hr->debug.flags[HR_DEBUG_DUMP_TEXTURES] =
		vk_util_get_bool_option ("HR_DUMP_TEXTURES", false) ? 1 : 0;
}

static void
update_debug_flags (hikaru_renderer_t *hr)
{
	char *msg = hr->base.message;
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++) {
		uint32_t key = debug_controls[i].key;
		if (key != ~0 && vk_input_get_key (key)) {
			hr->debug.flags[i] += 1;
			if (i == HR_DEBUG_DETWIDDLE_TEXTURES)
				hikaru_renderer_invalidate_texcache (&hr->base, NULL);
			if (hr->debug.flags[i] > debug_controls[i].max)
				hr->debug.flags[i] = debug_controls[i].min;
			if (debug_controls[i].print)
				VK_LOG ("HR DEBUG: '%s' = %d\n",
				        debug_controls[i].name, hr->debug.flags[i]);
		}
		msg += sprintf (msg, "%d|", hr->debug.flags[i]);
	}
}

/****************************************************************************
 State
****************************************************************************/

static bool
is_viewport_set (hikaru_viewport_t *vp)
{
	return true; // vp && (vp->flags & 0x27) == 0x27;
}

static bool
is_viewport_valid (hikaru_viewport_t *vp)
{
	if (!is_viewport_set (vp))
		return false;

	if (!ispositive (vp->clip.l) || !ispositive (vp->clip.r) ||
	    !ispositive (vp->clip.b) || !ispositive (vp->clip.t) ||
	    !ispositive (vp->clip.f) || !ispositive (vp->clip.n))
		return false;

	if ((vp->clip.l >= vp->clip.r) ||
	    (vp->clip.b >= vp->clip.t) ||
	    (vp->clip.n >= vp->clip.f))
		return false;

	if (!ispositive (vp->offset.x) || (vp->offset.x >= 640.0f) ||
	    !ispositive (vp->offset.y) || (vp->offset.y >= 480.0f))
		return false;

	return true;
}

static void
upload_viewport (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_viewport_t *vp =
		(mesh->vp_index == ~0) ? NULL : &hr->vp_list[mesh->vp_index];

	const float h = vp->clip.t - vp->clip.b;
	const float w = vp->clip.r - vp->clip.l;
	const float hh_at_n = (h / 2.0f) * (vp->clip.n / vp->clip.f);
	const float hw_at_n = hh_at_n * (w / h);
	const float dcx = vp->offset.x - (w / 2.0f);
	const float dcy = vp->offset.y - (h / 2.0f);

	LOG ("vp  = %s : [w=%f h=%f dcx=%f dcy=%f]",
	     get_viewport_str (vp), w, h, dcx, dcy);

	if (!is_viewport_valid (vp))
		VK_ERROR ("invalid viewport [%s]", get_viewport_str (vp));

	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glFrustum (-hw_at_n, hw_at_n, -hh_at_n, hh_at_n, vp->clip.n, 1e5);
	/* XXX scissor */
	glTranslatef (dcx, -dcy, 0.0f);
}

static void
upload_modelview (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, unsigned i)
{
	hikaru_modelview_t *mv =
		(mesh->mv_index == ~0) ? NULL : &hr->mv_list[mesh->mv_index + i];

	if (!mv)
		return;

	LOG ("mv  = %s", get_modelview_str (mv));

	glMatrixMode (GL_MODELVIEW);
	glLoadMatrixf ((GLfloat *) &mv->mtx[0][0]);
}

static bool
is_material_set (hikaru_material_t *mat)
{
	return true; // mat && (mat->flags & 0xEF) == 0xEF;
}

static bool
is_texhead_set (hikaru_texhead_t *th)
{
	return true; // th && (th->flags & 7) == 7;
}

static void
upload_material_texhead (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_material_t *mat =
		(mesh->mat_index == ~0) ? NULL : &hr->mat_list[mesh->mat_index];
	hikaru_texhead_t *th =
		(mesh->tex_index == ~0) ? NULL : &hr->tex_list[mesh->tex_index];
	bool has_texture = mat->has_texture && !hr->debug.flags[HR_DEBUG_NO_TEXTURES];

	if (!is_material_set (mat)) {
		VK_ERROR ("attempting to upload unset material");
		has_texture = false;
	} else
		LOG ("mat = %s", get_material_str (mat));

	if (!is_texhead_set (th)) {
		VK_ERROR ("attempting to upload unset texhead");
		has_texture = false;
	} else
		LOG ("tex = %s", get_texhead_str (th));

	if (has_texture) {
		vk_surface_t *surface;

		surface = hr->debug.flags[HR_DEBUG_USE_DEBUG_TEXTURE] ?
		          NULL : hikaru_renderer_decode_texture (&hr->base, th);

		if (!surface)
			surface = hr->textures.debug;

		vk_surface_bind (surface);
		glEnable (GL_TEXTURE_2D);
	} else
		glDisable (GL_TEXTURE_2D);
}

static bool
is_light_set (hikaru_light_t *lit)
{
	return true; // lit && (lit->flags & 0x1F) == 0x1F;
}

static hikaru_light_type_t
get_light_type (hikaru_light_t *lit)
{
	VK_ASSERT (lit->has_direction || lit->has_position);
	if (lit->has_direction && lit->has_position)
		return HIKARU_LIGHT_TYPE_SPOT;
	else if (lit->has_position)
		return HIKARU_LIGHT_TYPE_POSITIONAL;
	return HIKARU_LIGHT_TYPE_DIRECTIONAL;
}

static hikaru_light_att_t
get_light_attenuation_type (hikaru_light_t *lit)
{
	if (lit->att_type == 0 &&
	    lit->attenuation[0] == 1.0f &&
	    lit->attenuation[1] == 1.0f)
		return HIKARU_LIGHT_ATT_INF;
	return lit->att_type;
}

static void
get_light_attenuation (hikaru_renderer_t *hr, hikaru_light_t *lit, float *out)
{
	float min, max;

	/* XXX OpenGL fixed-function attenuation model can't represent most
	 * Hikaru light models... */

	switch (get_light_attenuation_type (lit)) {
	case HIKARU_LIGHT_ATT_LINEAR:
		VK_ASSERT (lit->attenuation[0] < 0.0f);
		VK_ASSERT (lit->attenuation[1] < 0.0f);
		min = -lit->attenuation[1];
		max = min + 1.0f / lit->attenuation[0];
		out[0] = 0.0f;
		out[1] = 1.0f / min;
		out[2] = 0.0f;
		break;
	case HIKARU_LIGHT_ATT_SQUARE:
		/* Used in BRAVEFF */
	case HIKARU_LIGHT_ATT_INVLINEAR:
	case HIKARU_LIGHT_ATT_INVSQUARE:
	default:
		out[0] = 0.0f;
		out[1] = 0.2f;
		out[2] = 0.0f;
		break;
	case HIKARU_LIGHT_ATT_INF:
		out[0] = 1.0f;
		out[1] = 0.0f;
		out[2] = 0.0f;
		break;
	}
}

static void
get_light_ambient (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, float *out)
{
	hikaru_viewport_t *vp =
		(mesh->vp_index == ~0) ? NULL : &hr->vp_list[mesh->vp_index];

	if (hr->debug.flags[HR_DEBUG_NO_AMBIENT] || !vp)
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = vp->color.ambient[0] * INV255;
		out[1] = vp->color.ambient[1] * INV255;
		out[2] = vp->color.ambient[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_light_diffuse (hikaru_renderer_t *hr, hikaru_light_t *lit, float *out)
{
	/* NOTE: the index uploaded with the diffuse color may be related
	 * to the table uploaded by instruction 194 (which may contain alpha
	 * values, or alpha ramps, or something...) */

	if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE])
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = lit->diffuse[0] * INV255;
		out[1] = lit->diffuse[1] * INV255;
		out[2] = lit->diffuse[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_light_specular (hikaru_renderer_t *hr, hikaru_light_t *lit, float *out)
{
	if (!lit->has_specular || hr->debug.flags[HR_DEBUG_NO_SPECULAR])
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = lit->specular[0] * INV255;
		out[1] = lit->specular[1] * INV255;
		out[2] = lit->specular[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_material_diffuse (hikaru_renderer_t *hr, hikaru_material_t *mat, float *out)
{
	if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE]) {
		out[0] = out[1] = out[2] = 0.0f;
	} else {
		out[0] = mat->diffuse[0] * INV255;
		out[1] = mat->diffuse[1] * INV255;
		out[2] = mat->diffuse[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_material_ambient (hikaru_renderer_t *hr, hikaru_material_t *mat, float *out)
{
	if (hr->debug.flags[HR_DEBUG_NO_AMBIENT]) {
		out[0] = out[1] = out[2] = 0.0f;
	} else {
		out[0] = mat->ambient[0] * INV255;
		out[1] = mat->ambient[1] * INV255;
		out[2] = mat->ambient[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_material_specular (hikaru_renderer_t *hr, hikaru_material_t *mat, float *out)
{
	if (hr->debug.flags[HR_DEBUG_NO_SPECULAR]) {
		out[0] = out[1] = out[2] = out[3] = 0.0f;
	} else {
		out[0] = mat->specular[0] * INV255;
		out[1] = mat->specular[1] * INV255;
		out[2] = mat->specular[2] * INV255;
		out[3] = mat->specular[3] * INV255 * 128.0f;
	}
}

static void
upload_lightset (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_material_t *mat = &hr->mat_list[mesh->mat_index];
	hikaru_lightset_t *ls = &hr->ls_list[mesh->ls_index];
	GLfloat tmp[4];
	unsigned i, n;

	if (hr->debug.flags[HR_DEBUG_NO_LIGHTING])
		goto disable;

	if (mesh->ls_index >= MAX_LIGHTSETS) {
		VK_ERROR ("attempting to upload NULL lightset!");
		goto disable;
	}

	if (mesh->mat_index >= MAX_MATERIALS) {
		VK_ERROR ("attempting to upload lightset with NULL material!");
		goto disable;
	}

	if (!ls->set) {
		VK_ERROR ("attempting to use unset lightset!");
		goto disable;
	}

	if (ls->mask == 0xF) {
		VK_ERROR ("attempting to use lightset with no light!");
		goto disable;
	}

	/* If the material is unset, treat it as shading_mode is 1; that way
	 * we can actually check lighting in the viewer. */
	if (is_material_set (mat) && mat->shading_mode == 0)
		goto disable;

	/* Lights are positioned according to the scene, irrespective of
	 * the modelview matrix. */
	glEnable (GL_LIGHTING);

	get_light_ambient (hr, mesh, tmp);
	glLightModelfv (GL_LIGHT_MODEL_AMBIENT, tmp);

	/* For each of the four lights in the current lightset */
	for (i = 0; i < 4; i++) {
		hikaru_light_t *lt;

		if (ls->mask & (1 << i))
			continue;

		lt = &ls->lights[i];
		if (!is_light_set (lt)) {
			VK_ERROR ("attempting to use unset light!");
			continue;
		}

		LOG ("light%u = enabled, %s", i, get_light_str (lt));

		n = GL_LIGHT0 + i;

		if ((hr->debug.flags[HR_DEBUG_SELECT_ATT_TYPE] < 0) ||
		    (hr->debug.flags[HR_DEBUG_SELECT_ATT_TYPE] == get_light_attenuation_type (lt)))
			glEnable (n);
		else
			glDisable (n);

		get_light_diffuse (hr, lt, tmp);
		glLightfv (n, GL_DIFFUSE, tmp);
		get_light_specular (hr, lt, tmp);
		glLightfv (n, GL_SPECULAR, tmp);

		switch (get_light_type (lt)) {
		case HIKARU_LIGHT_TYPE_DIRECTIONAL:
			tmp[0] = lt->direction[0];
			tmp[1] = lt->direction[1];
			tmp[2] = lt->direction[2];
			tmp[3] = 0.0f;
			glLightfv (n, GL_POSITION, tmp);
			break;
		case HIKARU_LIGHT_TYPE_POSITIONAL:
			tmp[0] = lt->position[0];
			tmp[1] = lt->position[1];
			tmp[2] = lt->position[2];
			tmp[3] = 1.0f;
			glLightfv (n, GL_POSITION, tmp);
			break;
		case HIKARU_LIGHT_TYPE_SPOT:
			glPushMatrix ();
			glLoadIdentity ();

			/* XXX this is very bogus. */

			tmp[0] = lt->direction[0];
			tmp[1] = lt->direction[1];
			tmp[2] = lt->direction[2];
			glTranslatef (tmp[0], tmp[1], tmp[2]);

			tmp[0] = lt->position[0];
			tmp[1] = lt->position[1];
			tmp[2] = lt->position[2];
			tmp[3] = 1.0f;
			glLightfv (n, GL_POSITION, tmp);

			glLightf (n, GL_SPOT_EXPONENT, 128.0f);

			glPopMatrix ();
			break;
		default:
			VK_ASSERT (!"unreachable");
		}

		get_light_attenuation (hr, lt, tmp);
		glLightf (n, GL_CONSTANT_ATTENUATION, tmp[0]);
		glLightf (n, GL_LINEAR_ATTENUATION, tmp[1]);
		glLightf (n, GL_QUADRATIC_ATTENUATION, tmp[2]);
	}

	/* We upload the material properties here, as we don't store all
	 * of them in the vertex_t yet (we will when we upgrade the renderer
	 * to GL 3.0 and GLSL). */

	get_material_diffuse (hr, mat, tmp);
	glMaterialfv (GL_FRONT_AND_BACK, GL_DIFFUSE, tmp);
	get_material_ambient (hr, mat, tmp);
	glMaterialfv (GL_FRONT_AND_BACK, GL_AMBIENT, tmp);
	get_material_specular (hr, mat, tmp);
	glMaterialfv (GL_FRONT_AND_BACK, GL_SPECULAR, tmp);
	glMaterialf (GL_FRONT_AND_BACK, GL_SHININESS, tmp[3]);
	return;

disable:
	glDisable (GL_LIGHTING);
}

/****************************************************************************
 Meshes
****************************************************************************/

#define VK_COPY_VEC2F(dst_, src_) \
	do { \
		dst_[0] = src_[0]; \
		dst_[1] = src_[1]; \
	} while (0)

#define VK_COPY_VEC3F(dst_, src_) \
	do { \
		dst_[0] = src_[0]; \
		dst_[1] = src_[1]; \
		dst_[2] = src_[2]; \
	} while (0)

static float
clampf (float x, float min_, float max_)
{
	return (min_ > x) ? min_ :
	       (max_ < x) ? max_ : x;
}

static void
copy_colors (hikaru_renderer_t *hr, hikaru_vertex_t *dst, hikaru_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_material_t *mat = &MAT.scratch;
	hikaru_viewport_t *vp = &VP.scratch;
	float base_alpha = POLY.alpha;
	float mat_alpha = mat->diffuse[3] * INV255;
	float vertex_alpha = src->info.alpha * INV255;
	float alpha;

	switch (POLY.type) {
	case HIKARU_POLYTYPE_OPAQUE:
	case HIKARU_POLYTYPE_TRANSPARENT:
	default:
		alpha = 1.0f;
		break;
	case HIKARU_POLYTYPE_TRANSLUCENT:
		alpha = base_alpha * vertex_alpha;
		alpha = clampf (alpha, 0, 1);
		break;
	}
	dst->col[3] = alpha;

	if (is_material_set (mat)) {
		switch (hr->debug.flags[HR_DEBUG_SELECT_BASE_COLOR]) {
		case 0:
			dst->col[0] = mat->diffuse[0] * INV255;
			dst->col[1] = mat->diffuse[1] * INV255;
			dst->col[2] = mat->diffuse[2] * INV255;
			break;
		case 1:
			dst->col[0] = mat->ambient[0] * INV255;
			dst->col[1] = mat->ambient[1] * INV255;
			dst->col[2] = mat->ambient[2] * INV255;
			break;
		case 2:
			dst->col[0] = mat->specular[0] * INV255;
			dst->col[1] = mat->specular[1] * INV255;
			dst->col[2] = mat->specular[2] * INV255;
			break;
		case 3:
			dst->col[0] = mat->unknown[0] * INV255;
			dst->col[1] = mat->unknown[1] * INV255;
			dst->col[2] = mat->unknown[2] * INV255;
			break;
		case 4:
			dst->col[0] = base_alpha;
			dst->col[1] = base_alpha;
			dst->col[2] = base_alpha;
			dst->col[3] = 1.0f;
			break;
		case 5:
			dst->col[0] = mat_alpha;
			dst->col[1] = mat_alpha;
			dst->col[2] = mat_alpha;
			dst->col[3] = 1.0f;
			break;
		case 6:
			dst->col[0] = vertex_alpha;
			dst->col[1] = vertex_alpha;
			dst->col[2] = vertex_alpha;
			dst->col[3] = 1.0f;
			break;
		case 7:
			dst->col[0] = alpha;
			dst->col[1] = alpha;
			dst->col[2] = alpha;
			dst->col[3] = 1.0f;
			break;
		case 8:
			dst->col[0] = vp->color.ambient[0] * INV255;
			dst->col[1] = vp->color.ambient[1] * INV255;
			dst->col[2] = vp->color.ambient[2] * INV255;
			dst->col[3] = 1.0f;
			break;
		case 9:
			dst->col[0] = vp->color.clear[0] * INV255;
			dst->col[1] = vp->color.clear[1] * INV255;
			dst->col[2] = vp->color.clear[2] * INV255;
			dst->col[3] = 1.0f;
			break;
		}
	} else {
		dst->col[0] = 1.0f;
		dst->col[1] = 1.0f;
		dst->col[2] = 1.0f;
	}
}

static void
copy_texcoords (hikaru_renderer_t *hr,
                hikaru_vertex_t *dst, hikaru_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_texhead_t *th = &TEX.scratch;
	float w = 16 << th->logw;
	float h = 16 << th->logh;

	if (th->format == HIKARU_FORMAT_ABGR1111)
		h *= 2;

	if (is_texhead_set (th)) {
		dst->txc[0] = src->txc[0] / w;
		dst->txc[1] = src->txc[1] / h;
	} else {
		dst->txc[0] = 0.0f;
		dst->txc[1] = 0.0f;
	}
}

static void
add_triangle (hikaru_renderer_t *hr)
{
	if (hr->push.num_verts >= 3) {
		uint32_t index = hr->push.num_tris * 3;
		hikaru_vertex_t *dst = &hr->push.all[index];

		VK_ASSERT ((index + 2) < MAX_VERTICES_PER_MESH);

		if (hr->push.tmp[2].info.winding) {
			dst[0] = hr->push.tmp[0];
			dst[1] = hr->push.tmp[2];
			dst[2] = hr->push.tmp[1];
		} else {
			dst[0] = hr->push.tmp[0];
			dst[1] = hr->push.tmp[1];
			dst[2] = hr->push.tmp[2];
		}
		hr->push.num_tris += 1;
	}
}

void
hikaru_renderer_push_vertices (vk_renderer_t *rend,
                               hikaru_vertex_t *v,
                               uint32_t flags,
                               unsigned num)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	unsigned i;

	VK_ASSERT (hr);
	VK_ASSERT (v);
	VK_ASSERT (num == 1 || num == 3);
	VK_ASSERT (v->info.tricap == 0 || v->info.tricap == 7);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	switch (num) {

	case 1:
		/* Note that VTX(2) always points to the last pushed vertex,
		 * which for instructions 12x, 1Ax and 1Bx means the vertex
		 * pushed by the instruction itself, and for instructions 1Ex
		 * and 15x the vertex pushed by the previous "push"
		 * instruction.
		 */

		/* If the incoming vertex includes the position, push it
		 * in the temporary buffer, updating it according to the
		 * p(osition)pivot bit. */
		if (flags & HR_PUSH_POS) {

			/* Do not change the pivot if it is not required */
			if (!v->info.ppivot)
				hr->push.tmp[0] = hr->push.tmp[1];
			hr->push.tmp[1] = hr->push.tmp[2];
			memset ((void *) &hr->push.tmp[2], 0, sizeof (hikaru_vertex_t));

			/* Set the position, colors and alpha. */
			VK_COPY_VEC3F (hr->push.tmp[2].pos, v->pos);
			copy_colors (hr, &hr->push.tmp[2], v);

			/* Account for the added vertex. */
			hr->push.num_verts += 1;
			VK_ASSERT (hr->push.num_verts < MAX_VERTICES_PER_MESH);
		}

		/* Set the normal. */
		if (flags & HR_PUSH_NRM) {
			VK_COPY_VEC3F (hr->push.tmp[2].nrm, v->nrm);

			/* DEBUG: overwrite the color with the normals. */
			if (hr->debug.flags[HR_DEBUG_DRAW_NORMALS]) {
				hr->push.tmp[2].col[0] = (v->nrm[0] * 0.5f) + 0.5f;
				hr->push.tmp[2].col[1] = (v->nrm[1] * 0.5f) + 0.5f;
				hr->push.tmp[2].col[2] = (v->nrm[2] * 0.5f) + 0.5f;
			}
		}

		/* Set the texcoords. */
		if (flags & HR_PUSH_TXC)
			copy_texcoords (hr, &hr->push.tmp[2], v);
		break;

	case 3:
		VK_ASSERT (flags == HR_PUSH_TXC);

		if (hr->push.num_verts < 3)
			return;

		for (i = 0; i < 3; i++)
			copy_texcoords (hr, &hr->push.tmp[2 - i], &v[i]);
		break;

	default:
		VK_ASSERT (!"num is neither 1 nor 3");
		break;
	}

	/* Finish the previous triangle. */
	if (v[0].info.tricap == 7) {
		hr->push.tmp[2].info.full = v[0].info.full;
		add_triangle (hr);
	}
}

#define OFFSET(member_) \
	((const GLvoid *) offsetof (hikaru_vertex_t, member_))

static void
hikaru_mesh_upload_pushed_data (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	VK_ASSERT (mesh);
	VK_ASSERT (mesh->vbo);

	mesh->num_tris = hr->push.num_tris;

	glBindBuffer (GL_ARRAY_BUFFER, mesh->vbo);
	glBufferData (GL_ARRAY_BUFFER, sizeof (hikaru_vertex_t) * mesh->num_tris * 3,
	              (const GLvoid *) hr->push.all, GL_DYNAMIC_DRAW);
}

static void
draw_mesh (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	unsigned i;

	VK_ASSERT (mesh);
	VK_ASSERT (mesh->vbo);

	LOG ("==== DRAWING MESH (#vertices=%u) ====",
	     mesh->num_tris * 3);

	upload_viewport (hr, mesh);
	upload_material_texhead (hr, mesh);
	upload_lightset (hr, mesh);

	glBindBuffer (GL_ARRAY_BUFFER, mesh->vbo);

	glVertexPointer (3, GL_FLOAT, sizeof (hikaru_vertex_t), OFFSET (pos));
	glNormalPointer (GL_FLOAT, sizeof (hikaru_vertex_t), OFFSET (nrm));
	glColorPointer (4, GL_FLOAT,  sizeof (hikaru_vertex_t), OFFSET (col));
	glTexCoordPointer (2, GL_FLOAT,  sizeof (hikaru_vertex_t), OFFSET (txc));

	glEnableClientState (GL_VERTEX_ARRAY);
	glEnableClientState (GL_NORMAL_ARRAY);
	glEnableClientState (GL_COLOR_ARRAY);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);

	if (hr->debug.flags[HR_DEBUG_NO_INSTANCING]) {
		upload_modelview (hr, mesh, 0);
		glDrawArrays (GL_TRIANGLES, 0, mesh->num_tris * 3);
	} else {
		for (i = 0; i < mesh->num_instances; i++) {
			upload_modelview (hr, mesh, i);
			glDrawArrays (GL_TRIANGLES, 0, mesh->num_tris * 3);
		}
	}
}

#define VP0	VP.scratch
#define MAT0	MAT.scratch
#define TEX0	TEX.scratch
#define LS0	LIT.scratchset

static void
print_rendstate (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, char *prefix)
{
	if (mesh->vp_index < MAX_VIEWPORTS)
		LOG ("RENDSTATE %s %u vp:  %s", prefix, mesh->num,
		     get_viewport_str (&hr->vp_list[mesh->vp_index]));
	if (mesh->mv_index < MAX_MODELVIEWS)
		LOG ("RENDSTATE %s %u mv:  %s", prefix, mesh->num,
		     get_modelview_str (&hr->mv_list[mesh->mv_index]));
	if (mesh->mat_index < MAX_MATERIALS)
		LOG ("RENDSTATE %s %u mat: %s", prefix, mesh->num,
		     get_material_str (&hr->mat_list[mesh->mat_index]));
	if (mesh->tex_index < MAX_TEXHEADS)
		LOG ("RENDSTATE %s %u tex: %s", prefix, mesh->num,
		     get_texhead_str (&hr->tex_list[mesh->tex_index]));
	if (mesh->ls_index < MAX_LIGHTSETS)
		LOG ("RENDSTATE %s %u ls:  %s", prefix, mesh->num,
		     get_lightset_str (&hr->ls_list[mesh->ls_index]));
}

/* TODO check if more fine-grained uploaded tracking can help. */
/* TODO check boundary conditions when nothing is uploaded in a frame. */
/* TODO alpha threshold */
static void
update_and_set_rendstate (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned num_instances = MV.total;

	if (VP0.uploaded) {
		LOG ("RENDSTATE updating vp %u/%u", hr->num_vps, MAX_VIEWPORTS);
		hr->vp_list[hr->num_vps++] = VP0;
		VK_ASSERT (hr->num_vps < MAX_VIEWPORTS);
		VP0.uploaded = 0;
	}
	if (MAT0.uploaded) {
		LOG ("RENDSTATE updating mat %u/%u", hr->num_mats, MAX_MATERIALS);
		hr->mat_list[hr->num_mats++] = MAT0;
		VK_ASSERT (hr->num_mats < MAX_MATERIALS);
		MAT0.uploaded = 0;
	}
	if (TEX0.uploaded) {
		LOG ("RENDSTATE updating tex %u/%u", hr->num_texs, MAX_TEXHEADS);
		hr->tex_list[hr->num_texs++] = TEX0;
		VK_ASSERT (hr->num_texs < MAX_TEXHEADS);
		TEX0.uploaded = 0;
	}
	if (LS0.uploaded) {
		LOG ("RENDSTATE updating ls %u/%u", hr->num_lss, MAX_LIGHTSETS);
		hr->ls_list[hr->num_lss++] = LS0;
		VK_ASSERT (hr->num_lss < MAX_LIGHTSETS);
		LS0.uploaded = 0;
	}

	/* Copy the per-instance modelviews from last to first. */
	/* TODO optimize by setting MV.total to 0 (and fix the fallback). */
	mesh->num_instances = num_instances;
	while (num_instances > 0) {
		LOG ("RENDSTATE adding mv %u/%u [#instances=%u]",
		     hr->num_mvs, MAX_MODELVIEWS, num_instances);
		hr->mv_list[hr->num_mvs++] = MV.table[num_instances - 1];
		VK_ASSERT (hr->num_mvs < MAX_MODELVIEWS);
		num_instances -= 1;
	}

	mesh->vp_index = hr->num_vps - 1;
	mesh->mv_index = hr->num_mvs - 1;
	mesh->mat_index = hr->num_mats - 1;
	mesh->tex_index = hr->num_texs - 1;
	mesh->ls_index = hr->num_lss - 1;

	mesh->num = hr->total_meshes++;
	print_rendstate (hr, mesh, "U");
}

void
hikaru_renderer_begin_mesh (vk_renderer_t *rend, uint32_t addr,
                            bool is_static)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned polytype = POLY.type;
	hikaru_mesh_t *mesh;

	VK_ASSERT (hr);
	VK_ASSERT (!hr->meshes.current);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	/* Create a new mesh. */
	mesh = &hr->mesh_list[polytype][hr->num_meshes[polytype]++];
	VK_ASSERT (hr->num_meshes[polytype] < MAX_MESHES);

	glGenBuffers (1, &mesh->vbo);
	VK_ASSERT (mesh->vbo);

	mesh->addr[0] = addr;

	hr->meshes.current = mesh;
	update_and_set_rendstate (hr, mesh);

	/* Clear the push buffer. */
	hr->push.num_verts = 0;
	hr->push.num_tris = 0;
}

void
hikaru_renderer_end_mesh (vk_renderer_t *rend, uint32_t addr)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;

	VK_ASSERT (hr);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	VK_ASSERT (hr->meshes.current);

	hr->meshes.current->addr[1] = addr;
	hikaru_mesh_upload_pushed_data (hr, hr->meshes.current);

	hr->meshes.current = NULL;
}

static void
draw_meshes_for_polytype (hikaru_renderer_t *hr, int polytype)
{
	hikaru_mesh_t *meshes = hr->mesh_list[polytype];
	unsigned num = hr->num_meshes[polytype];
	int j;

	if (hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] >= 0 &&
	    hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] != polytype)
		goto destroy_meshes;

	LOG (" ==== DRAWING POLYTYPE %d ====", polytype);

	switch (polytype) {
	case HIKARU_POLYTYPE_TRANSPARENT:
	case HIKARU_POLYTYPE_TRANSLUCENT:
		glEnable (GL_BLEND);
		glDepthMask (GL_FALSE);
		break;
	default:
		glDisable (GL_BLEND);
		glDepthMask (GL_TRUE);
		break;
	}
	
	for (j = 0; j < num; j++) {
		hikaru_mesh_t *mesh = &meshes[j];
		print_rendstate (hr, mesh, "D");
		draw_mesh (hr, mesh);
	}

destroy_meshes:
	for (j = 0; j < num; j++) {
		hikaru_mesh_t *mesh = &meshes[j];
		if (mesh->vbo)
			glDeleteBuffers (1, &mesh->vbo);
	}
}

static void
draw_scene (hikaru_renderer_t *hr)
{
	static const int sorted_polytypes[] = {
		HIKARU_POLYTYPE_BACKGROUND,
		HIKARU_POLYTYPE_SHADOW_A,
		HIKARU_POLYTYPE_SHADOW_B,
		HIKARU_POLYTYPE_OPAQUE,
		HIKARU_POLYTYPE_TRANSPARENT,
		HIKARU_POLYTYPE_TRANSLUCENT,
	};
	unsigned i;

	glEnable (GL_DEPTH_TEST);
	glDepthFunc (GL_LESS);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	switch (hr->debug.flags[HR_DEBUG_SELECT_CULLFACE]) {
	case -1:
		glDisable (GL_CULL_FACE);
		break;
	case 0:
		glEnable (GL_CULL_FACE);
		glCullFace (GL_BACK);
		break;
	case 1:
		glEnable (GL_CULL_FACE);
		glCullFace (GL_FRONT);
		break;
	}

	for (i = 0; i < NUMELEM (sorted_polytypes); i++)
		draw_meshes_for_polytype (hr, sorted_polytypes[i]);

	glDepthMask (GL_TRUE);
}

/****************************************************************************
 Interface
****************************************************************************/

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	unsigned i;

	hr->num_vps = 0;
	hr->num_mvs = 0;
	hr->num_mats = 0;
	hr->num_texs = 0;
	hr->num_lss = 0;

	for (i = 0; i < 8; i++)
		hr->num_meshes[i] = 0;
	hr->total_meshes = 0;

	/* Fill in the debug stuff. */
	update_debug_flags (hr);

	/* clear the frame buffer to a bright pink color */
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Reset the modelview matrix */
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	/* Draw the background layers. */
	hikaru_renderer_draw_layers (hr, true);
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	draw_scene (hr);

	/* Draw the foreground layers. */
	hikaru_renderer_draw_layers (hr, false);

	LOG (" ==== RENDSTATE STATISTICS ==== ");
	LOG ("  vp  : %u", hr->num_vps);
	LOG ("  mv  : %u", hr->num_mvs);
	LOG ("  mat : %u", hr->num_mats);
	LOG ("  tex : %u", hr->num_texs);
	LOG ("  ls  : %u", hr->num_lss);
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_invalidate_texcache (renderer, NULL);
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;
		unsigned i;

		free (hr->vp_list);
		free (hr->mv_list);
		free (hr->mat_list);
		free (hr->tex_list);
		free (hr->ls_list);

		for (i = 0; i < 8; i++)
			free (hr->mesh_list[i]);

		vk_surface_destroy (&hr->textures.debug);
		hikaru_renderer_invalidate_texcache (*renderer_, NULL);
	}
}

static vk_surface_t *
build_debug_surface (void)
{
	/* Build a colorful 2x2 checkerboard surface */
	vk_surface_t *surface = vk_surface_new (2, 2, VK_SURFACE_FORMAT_RGBA4444, -1, -1);
	if (!surface)
		return NULL;
	vk_surface_put16 (surface, 0, 0, 0xF00F);
	vk_surface_put16 (surface, 0, 1, 0x0F0F);
	vk_surface_put16 (surface, 1, 0, 0x00FF);
	vk_surface_put16 (surface, 1, 1, 0xFFFF);
	vk_surface_commit (surface);
	return surface;
}

vk_renderer_t *
hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2])
{
	hikaru_renderer_t *hr;
	int i, ret;

	hr = ALLOC (hikaru_renderer_t);
	if (!hr)
		goto fail;

	hr->base.width = 640;
	hr->base.height = 480;

	hr->base.destroy = hikaru_renderer_destroy;
	hr->base.reset = hikaru_renderer_reset;
	hr->base.begin_frame = hikaru_renderer_begin_frame;
	hr->base.end_frame = hikaru_renderer_end_frame;

	ret = vk_renderer_init ((vk_renderer_t *) hr);
	if (ret)
		goto fail;

	hr->vp_list  = (hikaru_viewport_t *) malloc (sizeof (hikaru_viewport_t) * MAX_VIEWPORTS);
	hr->mv_list  = (hikaru_modelview_t *) malloc (sizeof (hikaru_modelview_t) * MAX_MODELVIEWS);
	hr->mat_list = (hikaru_material_t *) malloc (sizeof (hikaru_material_t) * MAX_MATERIALS);
	hr->tex_list = (hikaru_texhead_t *) malloc (sizeof (hikaru_texhead_t) * MAX_TEXHEADS);
	hr->ls_list  = (hikaru_lightset_t *) malloc (sizeof (hikaru_lightset_t) * MAX_LIGHTSETS);
	if (!hr->vp_list || !hr->mv_list ||
	    !hr->mat_list || !hr->tex_list || !hr->ls_list)
		goto fail;

	for (i = 0; i < 8; i++) {
		hr->mesh_list[i] = (hikaru_mesh_t *) malloc (sizeof (hikaru_mesh_t) * MAX_MESHES);
		if (!hr->mesh_list[i])
			goto fail;
	}

	init_debug_flags (hr);

	hr->textures.debug = build_debug_surface ();
	if (!hr->textures.debug)
		goto fail;

	glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
	glShadeModel (GL_SMOOTH);

	return (vk_renderer_t *) hr;

fail:
	hikaru_renderer_destroy ((vk_renderer_t **) &hr);
	return NULL;
}

void
hikaru_renderer_set_gpu (vk_renderer_t *renderer, void *gpu_as_void)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) gpu_as_void;

	hr->gpu = gpu;
}
