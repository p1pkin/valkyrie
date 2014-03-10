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

/****************************************************************************
 Debug
****************************************************************************/

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
	[HR_DEBUG_SELECT_BASE_COLOR]	= {  0, 3, SDLK_c,  true, "SELECT BASE COLOR" },
	[HR_DEBUG_SELECT_CULLFACE]	= { -1, 1, SDLK_f,  true, "SELECT CULLFACE" },
	[HR_DEBUG_NO_TEXTURES]		= {  0, 1, SDLK_t, false, "NO TEXTURES" },
	[HR_DEBUG_USE_DEBUG_TEXTURE]	= {  0, 1, SDLK_y, false, "USE DEBUG TEXTURE" },
	[HR_DEBUG_DUMP_TEXTURES]	= {  0, 1,     ~0, false, "DUMP TEXTURES" },
	[HR_DEBUG_SELECT_POLYTYPE]	= { -1, 7, SDLK_p,  true, "SELECT POLYTYPE" },
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
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++) {
		uint32_t key = debug_controls[i].key;
		if (key != ~0 && vk_input_get_key (key)) {
			hr->debug.flags[i] += 1;
			if (hr->debug.flags[i] > debug_controls[i].max)
				hr->debug.flags[i] = debug_controls[i].min;
			if (debug_controls[i].print)
				VK_LOG ("HR DEBUG: '%s' = %d\n",
				        debug_controls[i].name, hr->debug.flags[i]);
		}
	}
}

/****************************************************************************
 Rendering State
****************************************************************************/

/* TODO check if more fine-grained uploaded tracking can help. */
/* TODO check boundary conditions when nothing is uploaded in a frame. */
static void
update_and_set_rendstate (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_viewport_t *vp = &VP.scratch;
	hikaru_gpu_modelview_t *mv = NULL;
	hikaru_gpu_material_t *mat = &MAT.scratch;
	hikaru_gpu_texhead_t *tex = &TEX.scratch;
	hikaru_gpu_lightset_t *ls = &LIT.scratchset;

	LOG ("updating rendstate...");

	if (vp->uploaded) {
		vp->uploaded = 0;
		vp->dirty = 1;
		VK_VECTOR_APPEND (hr->states.viewports, hikaru_gpu_viewport_t, *vp);
		LOG ("updated vp");
	}

	if (MV.total) {
		MV.total = 0;
		MV.depth = 0;
		/* TODO append all modelviews. */
		VK_VECTOR_APPEND (hr->states.modelviews, hikaru_gpu_modelview_t, MV.table[0]);
		mv = (hikaru_gpu_modelview_t *) VK_VECTOR_LAST (hr->states.modelviews);
		LOG ("updated mv");
	} else
		mv = NULL;

	if (mat->uploaded) {
		mat->uploaded = 0;
		mat->dirty = 1;
		VK_VECTOR_APPEND (hr->states.materials, hikaru_gpu_material_t, *mat);
		LOG ("updated mat");
	}

	if (tex->uploaded) {
		tex->uploaded = 0;
		tex->dirty = 1;
		VK_VECTOR_APPEND (hr->states.texheads, hikaru_gpu_texhead_t, *tex);
		LOG ("updated tex");
	}

	if (ls->uploaded) {
		ls->uploaded = 0;
		ls->dirty = 1;
		VK_VECTOR_APPEND (hr->states.lightsets, hikaru_gpu_lightset_t, *ls);
		LOG ("updated ls");
	}

	mesh->vp =
		(hikaru_gpu_viewport_t *) VK_VECTOR_LAST (hr->states.viewports);
	mesh->mv = mv;
	mesh->mat =
		(hikaru_gpu_material_t *) VK_VECTOR_LAST (hr->states.materials);
	mesh->tex =
		(hikaru_gpu_texhead_t *) VK_VECTOR_LAST (hr->states.texheads);
	mesh->ls =
		(hikaru_gpu_lightset_t *) VK_VECTOR_LAST (hr->states.lightsets);

	LOG ("rendstate = { vp=%p mv=%p mat=%p tex=%p ls=%p }",
	     mesh->vp, mesh->mv, mesh->mat, mesh->tex, mesh->ls);
}

/****************************************************************************
 Viewports
****************************************************************************/

static bool
is_viewport_valid (hikaru_gpu_viewport_t *vp)
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
upload_current_viewport (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_viewport_t *vp = mesh->vp;

	const float h = vp->clip.t - vp->clip.b;
	const float w = vp->clip.r - vp->clip.l;
	const float hh_at_n = (h / 2.0f) * (vp->clip.n / vp->clip.f);
	const float hw_at_n = hh_at_n * (w / h);
	const float dcx = vp->offset.x - (w / 2.0f);
	const float dcy = vp->offset.y - (h / 2.0f);

	if (!vp->dirty)
		return;

	vp->dirty = 0;

	LOG ("vp  = %s : [w=%f h=%f dcx=%f dcy=%f]",
	     get_gpu_viewport_str (vp), w, h, dcx, dcy);

	if (!is_viewport_valid (vp))
		VK_ERROR ("invalid viewport [%s]", get_gpu_viewport_str (vp));

	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glFrustum (-hw_at_n, hw_at_n, -hh_at_n, hh_at_n, vp->clip.n, 1e5);
	/* XXX scissor */
	glTranslatef (dcx, -dcy, 0.0f);

	glEnable (GL_DEPTH_TEST);
	//glDepthFunc (depth_func[vp->depth.func]);
}

/****************************************************************************
 Modelviews
****************************************************************************/

static void
upload_current_modelview (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	LOG ("mv  = %s", get_gpu_modelview_str (mesh->mv));

	if (mesh->mv) {
		glMatrixMode (GL_MODELVIEW);
		glLoadMatrixf ((GLfloat *) &mesh->mv->mtx[0][0]);
	}
}

/****************************************************************************
 Materials & Texheads
****************************************************************************/

static void
upload_current_material_texhead (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_material_t *mat = mesh->mat;
	hikaru_gpu_texhead_t *th = mesh->tex;

	if (!mat->dirty && !th->dirty)
		return;

	mat->dirty = 0;
	th->dirty = 0;

	LOG ("mat = %s", get_gpu_material_str (mat));
	if (is_material_set (mat) && mat->has_texture)
		LOG ("th  = %s", get_gpu_texhead_str (th));

	if (hr->debug.flags[HR_DEBUG_NO_TEXTURES] ||
	    !(is_material_set (mat) && mat->has_texture && is_texhead_set (th)))
		glDisable (GL_TEXTURE_2D);
	else {
		vk_surface_t *surface;

		surface = hr->debug.flags[HR_DEBUG_USE_DEBUG_TEXTURE] ?
		          NULL : hikaru_renderer_decode_texture (&hr->base, th);

		if (!surface)
			surface = hr->textures.debug;

		vk_surface_bind (surface);
		glEnable (GL_TEXTURE_2D);
	}
}

/****************************************************************************
 Lights
****************************************************************************/

#define INV255	(1.0f / 255.0f)

typedef enum {
	HIKARU_LIGHT_TYPE_DIRECTIONAL,
	HIKARU_LIGHT_TYPE_POSITIONAL,
	HIKARU_LIGHT_TYPE_SPOT,

	HIKARU_NUM_LIGHT_TYPES
} hikaru_light_type_t;

typedef enum {
	HIKARU_LIGHT_ATT_LINEAR = 0,
	HIKARU_LIGHT_ATT_SQUARE = 1,
	HIKARU_LIGHT_ATT_INVLINEAR = 2,
	HIKARU_LIGHT_ATT_INVSQUARE = 3,
	HIKARU_LIGHT_ATT_INF = 4,

	HIKARU_NUM_LIGHT_ATTS
} hikaru_light_att_t;

static hikaru_light_type_t
get_light_type (hikaru_gpu_light_t *lit)
{
	VK_ASSERT (lit->has_direction || lit->has_position);
	if (lit->has_direction && lit->has_position)
		return HIKARU_LIGHT_TYPE_SPOT;
	else if (lit->has_position)
		return HIKARU_LIGHT_TYPE_POSITIONAL;
	return HIKARU_LIGHT_TYPE_DIRECTIONAL;
}

static hikaru_light_att_t
get_light_attenuation_type (hikaru_gpu_light_t *lit)
{
	if (lit->att_type == 0 &&
	    lit->attenuation[0] == 1.0f &&
	    lit->attenuation[1] == 1.0f)
		return HIKARU_LIGHT_ATT_INF;
	return lit->att_type;
}

static void
get_light_attenuation (hikaru_renderer_t *hr, hikaru_gpu_light_t *lit, float *out)
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
get_light_ambient (hikaru_renderer_t *hr, float *out)
{
	hikaru_gpu_t *gpu = hr->gpu;

	if (hr->debug.flags[HR_DEBUG_NO_AMBIENT])
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = VP.scratch.color.ambient[0] * INV255;
		out[1] = VP.scratch.color.ambient[1] * INV255;
		out[2] = VP.scratch.color.ambient[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_light_diffuse (hikaru_renderer_t *hr, hikaru_gpu_light_t *lit, float *out)
{
	/* NOTE: the index uploaded with the diffuse color may be related
	 * to the table uploaded by instruction 194 (which may contain alpha
	 * values, or alpha ramps, or something...) */

	if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE])
		out[0] = out[1] = out[2] = 1.0f;
	else {
		out[0] = lit->diffuse[0] * INV255;
		out[1] = lit->diffuse[1] * INV255;
		out[2] = lit->diffuse[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_light_specular (hikaru_renderer_t *hr, hikaru_gpu_light_t *lit, float *out)
{
	if (hr->debug.flags[HR_DEBUG_NO_SPECULAR])
		out[0] = out[1] = out[2] = 1.0f;
	else {
		out[0] = lit->specular[0] * INV255;
		out[1] = lit->specular[1] * INV255;
		out[2] = lit->specular[2] * INV255;
	}
	out[3] = 1.0f;
}

static void
get_material_diffuse (hikaru_renderer_t *hr, hikaru_gpu_material_t *mat, float *out)
{
	if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE]) {
		out[0] = out[1] = out[2] = 0.0f;
		out[3] = 1.0f;
	} else {
		out[0] = mat->diffuse[0] * INV255;
		out[1] = mat->diffuse[1] * INV255;
		out[2] = mat->diffuse[2] * INV255;
		out[3] = mat->diffuse[3] * INV255;
	}
}

static void
get_material_ambient (hikaru_renderer_t *hr, hikaru_gpu_material_t *mat, float *out)
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
get_material_specular (hikaru_renderer_t *hr, hikaru_gpu_material_t *mat, float *out)
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
upload_current_lightset (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_material_t *mat = mesh->mat;
	hikaru_gpu_lightset_t *ls = mesh->ls;
	GLfloat tmp[4];
	unsigned i, n;

	if (hr->debug.flags[HR_DEBUG_NO_LIGHTING])
		goto disable;

	if (!ls->dirty)
		return;

	ls->dirty = 0;

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
	glMatrixMode (GL_MODELVIEW);
	glPushMatrix ();
	glLoadIdentity ();

	glEnable (GL_LIGHTING);

	get_light_ambient (hr, tmp);
	glLightModelf (GL_LIGHT_MODEL_TWO_SIDE, 1.0f);
	glLightModelfv (GL_LIGHT_MODEL_AMBIENT, tmp);

	/* For each of the four lights in the current lightset */
	for (i = 0; i < 4; i++) {
		hikaru_gpu_light_t *lt;

		if (ls->mask & (1 << i))
			continue;

		lt = &ls->lights[i];
		if (!is_light_set (lt)) {
			VK_ERROR ("attempting to use unset light!");
			continue;
		}

		LOG ("light%u = enabled, %s", i, get_gpu_light_str (lt));

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

			tmp[0] = lt->direction[0];
			tmp[1] = lt->direction[1];
			tmp[2] = lt->direction[2];
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

	glPopMatrix ();
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
copy_colors (hikaru_renderer_t *hr, hikaru_gpu_vertex_t *dst, hikaru_gpu_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_material_t *mat = &MAT.scratch;
	float base_alpha = POLY.alpha;
	float mat_alpha = mat->diffuse[3] * INV255;
	float vertex_alpha = src->info.bit.alpha;

	/* XXX at the moment we use only color 1 (it's responsible for the
	 * BOOTROM CRT test). */

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
		default:
			dst->col[0] = mat->unknown[0] * INV255;
			dst->col[1] = mat->unknown[1] * INV255;
			dst->col[2] = mat->unknown[2] * INV255;
			break;
		}
	} else {
		dst->col[0] = 1.0f;
		dst->col[1] = 1.0f;
		dst->col[2] = 1.0f;
	}

	switch (POLY.type) {
	case HIKARU_POLYTYPE_OPAQUE:
	case HIKARU_POLYTYPE_TRANSPARENT:
	default:
		dst->col[3] = 1.0f;
		break;
	case HIKARU_POLYTYPE_TRANSLUCENT:
		/* XXX mmm, this equation doesn't look great in PHARRIER... */
		dst->col[3] = clampf (base_alpha + mat_alpha + vertex_alpha, 0.0f, 1.0f);
		break;
	}
}

static void
copy_texcoords (hikaru_renderer_t *hr,
                hikaru_gpu_vertex_t *dst, hikaru_gpu_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_texhead_t *th = &TEX.scratch;
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
		hikaru_gpu_vertex_t *dst = &hr->push.all[index];

		VK_ASSERT ((index + 2) < MAX_VERTICES_PER_MESH);

		if (hr->push.tmp[2].info.bit.winding) {
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
                               hikaru_gpu_vertex_t *v,
                               uint32_t flags,
                               unsigned num)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	unsigned i;

	VK_ASSERT (hr);
	VK_ASSERT (v);
	VK_ASSERT (num == 1 || num == 3);
	VK_ASSERT (v->info.bit.tricap == 0 || v->info.bit.tricap == 7);

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
			if (!v->info.bit.ppivot)
				hr->push.tmp[0] = hr->push.tmp[1];
			hr->push.tmp[1] = hr->push.tmp[2];
			memset ((void *) &hr->push.tmp[2], 0, sizeof (hikaru_gpu_vertex_t));

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
	if (v[0].info.bit.tricap == 7) {
		hr->push.tmp[2].info.full = v[0].info.full;
		add_triangle (hr);
	}
}

static void
hikaru_mesh_destroy (hikaru_mesh_t **mesh_)
{
	if (mesh_) {
		hikaru_mesh_t *mesh = *mesh_;
		if (mesh->vbo);
			glDeleteBuffers (1, &mesh->vbo);
		free (mesh);
		*mesh_ = NULL;
	}
}

static hikaru_mesh_t *
hikaru_mesh_new (void)
{
	hikaru_mesh_t *mesh;

	mesh = ALLOC (hikaru_mesh_t);
	if (!mesh)
		return NULL;

	glGenBuffers (1, &mesh->vbo);
	if (!mesh->vbo)
		goto fail;

	return mesh;

fail:
	hikaru_mesh_destroy (&mesh);
	return NULL;
}

#define OFFSET(member_) \
	((const GLvoid *) offsetof (hikaru_gpu_vertex_t, member_))

static void
hikaru_mesh_upload_pushed_data (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	VK_ASSERT (mesh);
	VK_ASSERT (mesh->vbo);

	mesh->num_tris = hr->push.num_tris;

	glBindBuffer (GL_ARRAY_BUFFER, mesh->vbo);
	glBufferData (GL_ARRAY_BUFFER, sizeof (hikaru_gpu_vertex_t) * mesh->num_tris * 3,
	              (const GLvoid *) hr->push.all, GL_DYNAMIC_DRAW);

	glVertexPointer (3, GL_FLOAT, sizeof (hikaru_gpu_vertex_t), OFFSET (pos));
	glNormalPointer (GL_FLOAT, sizeof (hikaru_gpu_vertex_t), OFFSET (nrm));
	glColorPointer (4, GL_FLOAT,  sizeof (hikaru_gpu_vertex_t), OFFSET (col));
	glTexCoordPointer (2, GL_FLOAT,  sizeof (hikaru_gpu_vertex_t), OFFSET (txc));

	glEnableClientState (GL_VERTEX_ARRAY);
	glEnableClientState (GL_NORMAL_ARRAY);
	glEnableClientState (GL_COLOR_ARRAY);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
}

static void
hikaru_mesh_draw (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_t *gpu = hr->gpu;

	VK_ASSERT (mesh);
	VK_ASSERT (mesh->vbo);

	LOG ("==== DRAWING MESH (#vertices=%u) ====",
	     mesh->num_tris * 3);

	if (hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] >= 0 &&
	    hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] != POLY.type)
		return;

	/* Upload instance-invariant state. */
	upload_current_viewport (hr, mesh);
	upload_current_material_texhead (hr, mesh);

	switch (POLY.type) {
	case HIKARU_POLYTYPE_OPAQUE:
	default:
		glDisable (GL_BLEND);
		break;
	case HIKARU_POLYTYPE_TRANSPARENT:
	case HIKARU_POLYTYPE_TRANSLUCENT:
		glEnable (GL_BLEND);
		break;
	}
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable (GL_CULL_FACE);
	switch (hr->debug.flags[HR_DEBUG_SELECT_CULLFACE]) {
	case -1:
		glDisable (GL_CULL_FACE);
		break;
	case 0:
		glCullFace (GL_BACK);
		break;
	case 1:
		glCullFace (GL_FRONT);
		break;
	}

	/* Upload per-instance state. */
	upload_current_modelview (hr, mesh);
	upload_current_lightset (hr, mesh); /* This is not really per-instance... */

	/* Draw the mesh. */
	glDrawArrays (GL_TRIANGLES, 0, mesh->num_tris * 3);
}


void
hikaru_renderer_begin_mesh (vk_renderer_t *rend, uint32_t addr,
                            bool is_static)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	hikaru_mesh_t *mesh;

	VK_ASSERT (hr);
	VK_ASSERT (!hr->meshes.current);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	/* Create a new mesh. */
	mesh = hikaru_mesh_new ();
	VK_ASSERT (mesh);
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

	/* Upload the mesh. */
	hr->meshes.current->addr[1] = addr;
	hikaru_mesh_upload_pushed_data (hr, hr->meshes.current);

	/* Draw the mesh. */
	hikaru_mesh_draw (hr, hr->meshes.current);

	/* Destroy the mesh. */
	hikaru_mesh_destroy (&hr->meshes.current);
}

/****************************************************************************
 Interface
****************************************************************************/

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	vk_vector_clear_fast (hr->states.viewports);
	vk_vector_clear_fast (hr->states.modelviews);
	vk_vector_clear_fast (hr->states.materials);
	vk_vector_clear_fast (hr->states.texheads);
	vk_vector_clear_fast (hr->states.lightsets);

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

	/* Draw the foreground layers. */
	hikaru_renderer_draw_layers (hr, false);

	LOG (" ==== RENDSTATE STATISTICS ==== ");
	LOG ("  vp  : %u", hr->states.viewports->used / sizeof (hikaru_gpu_viewport_t));
	LOG ("  mv  : %u", hr->states.modelviews->used / sizeof (hikaru_gpu_modelview_t));
	LOG ("  mat : %u", hr->states.materials->used / sizeof (hikaru_gpu_material_t));
	LOG ("  tex : %u", hr->states.texheads->used / sizeof (hikaru_gpu_texhead_t));
	LOG ("  ls  : %u", hr->states.lightsets->used / sizeof (hikaru_gpu_lightset_t));
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

		if (hr->states.viewports)
			vk_vector_destroy (&hr->states.viewports);
		if (hr->states.modelviews)
			vk_vector_destroy (&hr->states.modelviews);
		if (hr->states.materials)
			vk_vector_destroy (&hr->states.materials);
		if (hr->states.texheads)
			vk_vector_destroy (&hr->states.texheads);
		if (hr->states.lightsets)
			vk_vector_destroy (&hr->states.lightsets);
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
	int ret;

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

	hr->states.viewports = vk_vector_new (8, sizeof (hikaru_gpu_viewport_t));
	if (!hr->states.viewports)
		goto fail;

	hr->states.modelviews = vk_vector_new (8, sizeof (hikaru_gpu_modelview_t));
	if (!hr->states.modelviews)
		goto fail;

	hr->states.materials = vk_vector_new (8, sizeof (hikaru_gpu_material_t));
	if (!hr->states.materials)
		goto fail;

	hr->states.texheads = vk_vector_new (8, sizeof (hikaru_gpu_texhead_t));
	if (!hr->states.texheads)
		goto fail;

	hr->states.lightsets = vk_vector_new (8, sizeof (hikaru_gpu_lightset_t));
	if (!hr->states.lightsets)
		goto fail;

	init_debug_flags (hr);

	hr->textures.debug = build_debug_surface ();
	if (!hr->textures.debug)
		goto fail;

	glClearColor (1.0f, 0.0f, 1.0f, 1.0f);
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
