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
#include "mach/hikaru/hikaru-gpu-private.h"

#define RESTART_INDEX	0xFFFF

/****************************************************************************
 Debug
****************************************************************************/

#define HR_DEBUG_LOG			(1 << 0)
#define HR_DEBUG_DISABLE_2D		(1 << 1)
#define HR_DEBUG_DISABLE_3D		(1 << 2)
#define HR_DEBUG_DRAW_TEXRAM		(1 << 3)
#define HR_DEBUG_DRAW_FB		(1 << 4)
#define HR_DEBUG_DISABLE_TEXTURES	(1 << 5)
#define HR_DEBUG_FORCE_DEBUG_TEXTURE	(1 << 6)
#define HR_DEBUG_FORCE_RAND_COLOR	(1 << 7)
#define HR_DEBUG_SELECT_MESH		(1 << 8)

static struct {
	uint32_t flag;
	uint32_t key;
	char env[64];
	bool default_;
} debug_controls[] = {
	{ HR_DEBUG_LOG,			~0,	"HR_LOG",		false },
	{ HR_DEBUG_DISABLE_2D,		SDLK_2,	"HR_DEBUG_DISABLE_2D",	false },
	{ HR_DEBUG_DISABLE_3D,		SDLK_3,	"HR_DEBUG_DISABLE_3D",	false },
	{ HR_DEBUG_DRAW_TEXRAM,		SDLK_r,	"HR_DEBUG_DRAW_TEXRAM",	false },
	{ HR_DEBUG_DRAW_FB,		SDLK_f,	"HR_DEBUG_DRAW_FB",	false },
	{ HR_DEBUG_DISABLE_TEXTURES,	SDLK_t,	"",			false },
	{ HR_DEBUG_FORCE_DEBUG_TEXTURE,	SDLK_d,	"",			false },
	{ HR_DEBUG_FORCE_RAND_COLOR,	SDLK_c, "",			false },
	{ HR_DEBUG_SELECT_MESH,		SDLK_s, "",			false },
};

static void
update_debug_flags (hikaru_renderer_t *hr)
{
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++) {
		uint32_t key = debug_controls[i].key;
		if (key != ~0 && vk_input_get_key (key))
			hr->debug.flags ^= debug_controls[i].flag;
	}

	if (vk_input_get_key (SDLK_KP_PERIOD))
		hr->debug.selected_mesh = 0;
	if (vk_input_get_key (SDLK_KP_PLUS))
		hr->debug.selected_mesh += 1;
	if (vk_input_get_key (SDLK_KP_MINUS))
		hr->debug.selected_mesh -= 1;
}

static void
read_debug_flags (hikaru_renderer_t *hr)
{
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++) {
		char *env = debug_controls[i].env;
		if (env[0] != '\0' &&
		    vk_util_get_bool_option (env, debug_controls[i].default_))
			hr->debug.flags |= debug_controls[i].flag;
	}

	VK_LOG ("HR: debug flags = %08X", hr->debug.flags);
}

#define LOG(fmt_, args_...) \
	do { \
		if (hr->debug.flags & HR_DEBUG_LOG) \
			fprintf (stdout, "\tHR: " fmt_"\n", ##args_); \
	} while (0)

static void
upload_fb (hikaru_renderer_t *hr)
{
	vk_buffer_t *fb = hr->gpu->fb;
	uint32_t x, y;

	VK_ASSERT (hr->textures.fb);

	for (y = 0; y < 2048; y++) {
		uint32_t yoffs = y * 4096;
		for (x = 0; x < 2048; x++) {
			uint32_t offs = yoffs + x * 2;
			uint32_t texel = vk_buffer_get (fb, 2, offs);
			vk_surface_put16 (hr->textures.fb, x ^ 1, y, texel);
		}
	}
	vk_surface_commit (hr->textures.fb);
}

static void
upload_texram (hikaru_renderer_t *hr)
{
	vk_buffer_t *texram[2];
	uint32_t x, y;

	VK_ASSERT (hr->textures.texram);

	texram[0] = hr->gpu->texram[0];
	texram[1] = hr->gpu->texram[1];

	for (y = 0; y < 1024; y++) {
		uint32_t yoffs = y * 4096;
		for (x = 0; x < 2048; x++) {
			uint32_t texel, offs = yoffs + x * 2;
			texel = vk_buffer_get (texram[0], 2, offs);
			vk_surface_put16 (hr->textures.texram, x ^ 1, y, texel);
			texel = vk_buffer_get (texram[1], 2, offs);
			vk_surface_put16 (hr->textures.texram, 1024 + (x ^ 1), y, texel);
		}
	}
	vk_surface_commit (hr->textures.texram);
}

/****************************************************************************
 Texhead Decoding
****************************************************************************/

static uint32_t
rgba1111_to_rgba4444 (uint8_t pixel)
{
	static const uint32_t table[16] = {
		0x0000, 0xF000, 0x0F00, 0xFF00,
		0x00F0, 0xF0F0, 0x0FF0, 0xFFF0,
		0x000F, 0xF00F, 0x0F0F, 0xFF0F,
		0x00FF, 0xF0FF, 0x0FFF, 0xFFFF,
	};

	return table[pixel & 15];
}

static vk_surface_t *
decode_texhead_rgba1111 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->bank];
	uint32_t basex, basey, x, y;
	vk_surface_t *surface;

	surface = vk_surface_new (texhead->width, texhead->height*2, VK_SURFACE_FORMAT_RGBA4444);
	if (!surface)
		return NULL;

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty); 

	for (y = 0; y < texhead->height; y ++) {
		for (x = 0; x < texhead->width; x += 4) {
			uint32_t offs = (basey + y) * 4096 + (basex + x);
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x + 0, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >> 28));
			vk_surface_put16 (surface, x + 1, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >> 24));
			vk_surface_put16 (surface, x + 0, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >> 20));
			vk_surface_put16 (surface, x + 1, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >> 16));
			vk_surface_put16 (surface, x + 2, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >> 12));
			vk_surface_put16 (surface, x + 3, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >>  8));
			vk_surface_put16 (surface, x + 2, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >>  4));
			vk_surface_put16 (surface, x + 3, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >>  0));
		}
	}
	return surface;
}

static uint16_t
abgr1555_to_rgba5551 (uint16_t c)
{
	uint16_t r, g, b, a;

	r = (c >>  0) & 0x1F;
	g = (c >>  5) & 0x1F;
	b = (c >> 10) & 0x1F;
	a = (c >> 15) & 1;

	return (r << 11) | (g << 6) | (b << 1) | a;
}

static vk_surface_t *
decode_texhead_abgr1555 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->bank];
	uint32_t basex, basey, x, y;
	vk_surface_t *surface;

	surface = vk_surface_new (texhead->width, texhead->height, VK_SURFACE_FORMAT_RGBA5551);
	if (!surface)
		return NULL;

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty); 

	for (y = 0; y < texhead->height; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < texhead->width; x += 2) {
			uint32_t offs  = base + x * 2;
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x+0, y, abgr1555_to_rgba5551 (texels >> 16));
			vk_surface_put16 (surface, x+1, y, abgr1555_to_rgba5551 (texels));
		}
	}
	return surface;
}

static uint16_t
abgr4444_to_rgba4444 (uint16_t c)
{
	uint16_t r, g, b, a;

	r = (c >>  0) & 0xF;
	g = (c >>  4) & 0xF;
	b = (c >>  8) & 0xF;
	a = (c >> 12) & 0xF;

	return (r << 12) | (g << 8) | (b << 4) | a;
}

static vk_surface_t *
decode_texhead_abgr4444 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->bank];
	uint32_t basex, basey, x, y;
	vk_surface_t *surface;

	surface = vk_surface_new (texhead->width, texhead->height, VK_SURFACE_FORMAT_RGBA4444);
	if (!surface)
		return NULL;

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty); 

	for (y = 0; y < texhead->height; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < texhead->width; x += 2) {
			uint32_t offs  = base + x * 2;
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x+0, y, abgr4444_to_rgba4444 (texels >> 16));
			vk_surface_put16 (surface, x+1, y, abgr4444_to_rgba4444 (texels >> 16));
		}
	}
	return surface;
}

static vk_surface_t *
decode_texhead_a8 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	/* TODO */
	return NULL;
}

static struct {
	hikaru_gpu_texhead_t	texhead;
	vk_surface_t		*surface;
} texcache[2][256][256];

static bool
is_texhead_eq (hikaru_renderer_t *hr,
               hikaru_gpu_texhead_t *a, hikaru_gpu_texhead_t *b)
{
	return memcmp ((void *) a, (void *) b, sizeof (*a)) == 0;
}

static vk_surface_t *
decode_texhead (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	hikaru_gpu_texhead_t *cached;
	vk_surface_t *surface = NULL;
	uint32_t bank, slotx, sloty;

	bank  = texhead->bank;
	slotx = texhead->slotx;
	sloty = texhead->sloty;

	/* Lookup the texhead in the cache. */
	cached = &texcache[bank][sloty][slotx].texhead;
	if (is_texhead_eq (hr, texhead, cached)) {
		surface = texcache[bank][sloty][slotx].surface;
		if (surface)
			return surface;
	}

	/* Texhead not cached, decode it. */
	switch (texhead->format) {
	case HIKARU_FORMAT_ABGR1555:
		surface = decode_texhead_abgr1555 (hr, texhead);
		break;
	case HIKARU_FORMAT_ABGR4444:
		surface = decode_texhead_abgr4444 (hr, texhead);
		break;
	case HIKARU_FORMAT_ABGR1111:
		surface = decode_texhead_rgba1111 (hr, texhead);
		break;
	case HIKARU_FORMAT_ALPHA8:
		surface = decode_texhead_a8 (hr, texhead);
		break;
	default:
		VK_ASSERT (0);
		break;
	}

	/* Cache the decoded texhead. */
	texcache[bank][sloty][slotx].texhead = *texhead;
	texcache[bank][sloty][slotx].surface = surface;

	/* Upload the surface to the GL. */
	vk_surface_commit (surface);
	return surface;
}

static void
clear_texture_cache (hikaru_renderer_t *hr)
{
	unsigned b, x, y;

	for (b = 0; b < 2; b++)
		for (y = 0; y < 64; y++)
			for (x = 0; x < 64; x++)
				vk_surface_destroy (&texcache[b][y][x].surface);

	memset ((void *) texcache, 0, sizeof (texcache));
}

/****************************************************************************
 3D Rendering
****************************************************************************/

static bool
is_viewport_valid (hikaru_gpu_viewport_t *vp)
{
	if (!(vp->flags & HIKARU_GPU_OBJ_SET))
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

#if 0
static float
vk_mtx3x3f_det (mtx3x3f_t a)
{
	float subdet0 = mv->mtx[1][1] * mv->mtx[2][2] - mv->mtx[2][1] * mv->mtx[1][2];
	float subdet1 = mv->mtx[1][0] * mv->mtx[2][2] - mv->mtx[1][2] * mv->mtx[2][0];
	float subdet2 = mv->mtx[1][0] * mv->mtx[2][1] - mv->mtx[1][1] * mv->mtx[2][0];

	return mv->mtx[0][0] * subdet0 - mv->mtx[0][1] * subdet1 + mv->mtx[0][2] * subdet2;
}
#endif

static void
upload_current_state (hikaru_renderer_t *hr)
{
	hikaru_gpu_viewport_t *vp = &hr->gpu->viewports.scratch;

	/* Debug */
	LOG ("==== CURRENT STATE ====");

	/* Viewport */
	if (vp->flags & HIKARU_GPU_OBJ_DIRTY)
	{
		static const GLenum depth_func[8] = {
			GL_NEVER,	/* 0 */
			GL_LESS,	/* 1 */
			GL_EQUAL,	/* 2 */
			GL_LEQUAL,	/* 3 */
			GL_GREATER,	/* 4 */
			GL_NOTEQUAL,	/* 5 */
			GL_GEQUAL,	/* 6 */
			GL_ALWAYS	/* 7 */
		};

		const float h = vp->clip.t - vp->clip.b;
		const float w = vp->clip.r - vp->clip.l;
		const float hh_at_n = (h / 2.0f) * (vp->clip.n / vp->clip.f);
		const float hw_at_n = hh_at_n * (w / h);
		const float dcx = vp->offset.x - (w / 2.0f);
		const float dcy = vp->offset.y - (h / 2.0f);

		LOG ("vp  = %s : [w=%f h=%f dcx=%f dcy=%f]",
		     get_gpu_viewport_str (vp), w, h, dcx, dcy);
		if (!is_viewport_valid (vp))
			LOG ("*** INVALID VIEWPORT!!! ***");

		glMatrixMode (GL_PROJECTION);
		glLoadIdentity ();
		glFrustum (-hw_at_n, hw_at_n, -hh_at_n, hh_at_n, vp->clip.n, vp->clip.f);
		/* XXX scissor */
		glTranslatef (dcx, -dcy, 0.0f);

		if (vp->depth.func) {
			glEnable (GL_DEPTH_TEST);
			glDepthFunc (depth_func[vp->depth.func]);
		} else
			glDisable (GL_DEPTH_TEST);

		vp->flags &= ~HIKARU_GPU_OBJ_DIRTY;
	}

	/* Modelview */
	{
		hikaru_gpu_modelview_t *mv = &hr->gpu->modelviews.stack[0];

		LOG ("mv  = %s", get_gpu_modelview_str (mv));

		glMatrixMode (GL_MODELVIEW);
		glLoadMatrixf ((GLfloat *) &mv->mtx[0][0]);
	}

	/* Material and Texhead */
	{
		hikaru_gpu_material_t *mat = &hr->gpu->materials.scratch;
		hikaru_gpu_texhead_t *th   = &hr->gpu->texheads.scratch;

		LOG ("mat = %s", get_gpu_material_str (mat));
		if (mat->set && mat->has_texture)
			LOG ("th  = %s", get_gpu_texhead_str (th));

		if ((hr->debug.flags & HR_DEBUG_DISABLE_TEXTURES) ||
		    !mat->set || !th->set || !mat->has_texture)
			glDisable (GL_TEXTURE_2D);
		else {
			vk_surface_t *surface;

			surface = (hr->debug.flags & HR_DEBUG_FORCE_DEBUG_TEXTURE) ?
			          NULL : decode_texhead (hr, th);

			if (!surface)
				surface = hr->textures.debug;

			vk_surface_bind (surface);
			glEnable (GL_TEXTURE_2D);
		}
	}
}

static void
draw_current_mesh (hikaru_renderer_t *hr)
{
	unsigned i, j;

	LOG ("==== DRAWING MESH (current=%u #vertices=%u) ====",
	     hr->debug.current_mesh, hr->mesh.num_pushed);

	if ((hr->debug.flags & HR_DEBUG_SELECT_MESH) &&
	    (hr->debug.current_mesh != hr->debug.selected_mesh))
		goto skip_;

	glEnable (GL_BLEND);

	glBegin (GL_TRIANGLES);
	for (i = 0; i < hr->mesh.num_tris; i++) {
		for (j = 0; j < 3; j++) {
			hikaru_gpu_vertex_t *v = &hr->mesh.vbo[i*3+j];

			glTexCoord2fv (v->txc);
			if (hr->debug.flags & HR_DEBUG_FORCE_RAND_COLOR) {
				float r = (rand () & 0xFF) / 255.0f;
				glColor4f (r, r, r, v->col[3]);
			} else
				glColor4fv (v->col);
			glVertex3fv (v->pos);
		}
	}
	glEnd ();

skip_:
	hr->debug.current_mesh++;
}

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

static void
copy_colors (hikaru_renderer_t *hr, hikaru_gpu_vertex_t *dst, hikaru_gpu_vertex_t *src)
{
	hikaru_gpu_material_t *mat = &hr->gpu->materials.scratch;

	/* XXX at the moment we use only color 1 (it's responsible for the
	 * BOOTROM CRT test). */
	/* XXX check component orter (it's probably BGR). */

	if (mat->set) {
		dst->col[0] = mat->color[1][0] / 255.0f;
		dst->col[1] = mat->color[1][1] / 255.0f;
		dst->col[2] = mat->color[1][2] / 255.0f;
	} else {
		dst->col[0] = 1.0f;
		dst->col[1] = 1.0f;
		dst->col[2] = 1.0f;
	}
	dst->col[3] = src->info.bit.alpha / 255.0f;
}

static void
copy_texcoords (hikaru_renderer_t *hr,
                hikaru_gpu_vertex_t *dst, hikaru_gpu_vertex_t *src)
{
	hikaru_gpu_texhead_t *th = &hr->gpu->texheads.scratch;
	float height = th->height;

	if (th->format == HIKARU_FORMAT_ABGR1111)
		height *= 2;

	if (th->set) {
		dst->txc[0] = src->txc[0] / th->width;
		dst->txc[1] = src->txc[1] / height;
	} else {
		dst->txc[0] = 0.0f;
		dst->txc[1] = 0.0f;
	}
}

#define VTX(n_)	hr->mesh.vtx[n_]

static void
add_triangle (hikaru_renderer_t *hr)
{
	if (hr->mesh.num_pushed >= 3) {
		unsigned i;

		LOG ("*** ADDING TRIANGLE [%u] ***", hr->mesh.num_tris);
		VK_ASSERT ((hr->mesh.num_tris*3+2) < MAX_VERTICES_PER_MESH);

		for (i = 0; i < 3; i++) {
			LOG (" %u : %s", i, get_gpu_vertex_str (&VTX (i)));
			hr->mesh.vbo[hr->mesh.num_tris*3+i] = VTX(i);
		}

		hr->mesh.num_tris++;
	}
}

void
hikaru_renderer_push_vertices (hikaru_renderer_t *hr,
                               hikaru_gpu_vertex_t *v,
                               uint32_t push,
                               unsigned num)
{
	unsigned i;

	VK_ASSERT (hr);
	VK_ASSERT (v);
	VK_ASSERT (num == 1 || num == 3);
	VK_ASSERT (v->info.bit.tricap == 0 || v->info.bit.tricap == 7);

	if (hr->debug.flags & HR_DEBUG_DISABLE_3D)
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
		if (push & HR_PUSH_POS) {

			/* Do not change the pivot if it is not required */
			if (!v->info.bit.ppivot)
				VTX(0) = VTX(1);
			VTX(1) = VTX(2);

			memset ((void *) &VTX(2), 0, sizeof (hikaru_gpu_vertex_t));

			/* Set the position, colors and alpha. */
			VK_COPY_VEC3F (VTX(2).pos, v->pos);
			copy_colors (hr, &VTX(2), v);

			/* Account for the added vertex. */
			hr->mesh.num_pushed++;
			VK_ASSERT (hr->mesh.num_pushed < MAX_VERTICES_PER_MESH);
		}

		/* Set the normal. */
		if (push & HR_PUSH_NRM)
			VK_COPY_VEC3F (VTX(2).nrm, v->nrm);

		/* Set the texcoords. */
		if (push & HR_PUSH_TXC)
			copy_texcoords (hr, &VTX(2), v);
		break;

	case 3:
		VK_ASSERT (push == HR_PUSH_TXC);

		if (hr->mesh.num_pushed < 3)
			return;

		for (i = 0; i < 3; i++)
			copy_texcoords (hr, &VTX(2-i), &v[i]);
		break;

	default:
		VK_ASSERT (!"num is neither 1 nor 3");
		break;
	}

	/* Finish the previous triangle. */
	if (v[0].info.bit.tricap == 7)
		add_triangle (hr);
}

void
hikaru_renderer_begin_mesh (hikaru_renderer_t *hr, uint32_t addr,
                            bool is_static)
{
	VK_ASSERT (hr);

	if (hr->debug.flags & HR_DEBUG_DISABLE_3D)
		return;

	memset ((void *) &hr->mesh, 0, sizeof (hr->mesh));
	hr->mesh.addr[0] = addr;
}

void
hikaru_renderer_end_mesh (hikaru_renderer_t *hr, uint32_t addr)
{
	VK_ASSERT (hr);

	if (hr->debug.flags & HR_DEBUG_DISABLE_3D)
		return;

	hr->mesh.addr[1] = addr;

	upload_current_state (hr);
	draw_current_mesh (hr);

	memset ((void *) &hr->mesh, 0, sizeof (hr->mesh));
}

/****************************************************************************
 2D Rendering
****************************************************************************/

static inline uint32_t
coords_to_offs_16 (uint32_t x, uint32_t y)
{
	return y * 4096 + x * 2;
}

static inline uint32_t
coords_to_offs_32 (uint32_t x, uint32_t y)
{
	return y * 4096 + x * 4;
}

static vk_surface_t *
decode_layer_argb1555 (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_buffer_t *fb = hr->gpu->fb;
	vk_surface_t *surface;
	uint32_t x, y;

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA5551);
	if (!surface)
		return NULL;

	for (y = 0; y < 480; y++) {
		uint32_t yoffs = (layer->y0 + y) * 4096;
		for (x = 0; x < 640; x += 2) {
			uint32_t offs = yoffs + layer->x0 * 4 + x * 2;
			uint32_t texels = vk_buffer_get (fb, 4, offs);
			vk_surface_put16 (surface, x+0, y, abgr1555_to_rgba5551 (texels >> 16));
			vk_surface_put16 (surface, x+1, y, abgr1555_to_rgba5551 (texels));
		}
	}
	return surface;
}

static vk_surface_t *
decode_layer_argb8888 (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_buffer_t *fb = hr->gpu->fb;
	vk_surface_t *surface;
	uint32_t x, y;

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA8888);
	if (!surface)
		return NULL;

	for (y = 0; y < 480; y++) {
		for (x = 0; x < 640; x++) {
			uint32_t offs = coords_to_offs_32 (layer->x0 + x, layer->y0 + y);
			uint32_t texel = vk_buffer_get (fb, 4, offs);
			vk_surface_put32 (surface, x, y, bswap32 (texel));
		}
	}
	return surface;
}

static vk_surface_t *
upload_layer (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;

	if (layer->format == HIKARU_FORMAT_ABGR8888)
		surface = decode_layer_argb8888 (hr, layer);
	else
		surface = decode_layer_argb1555 (hr, layer);

	vk_surface_commit (surface);
	return surface;
}

void
hikaru_renderer_draw_layer (vk_renderer_t *renderer, hikaru_gpu_layer_t *layer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	vk_surface_t *surface;

	VK_ASSERT (hr);
	VK_ASSERT (layer);

	if (hr->debug.flags & HR_DEBUG_DISABLE_2D)
		return;

	LOG ("drawing layer %s", get_gpu_layer_str (layer));

	glColor3f (1.0f, 1.0f, 1.0f);
	glDisable (GL_SCISSOR_TEST);
	glDisable (GL_BLEND);

	/* XXX cache the layers and use dirty rectangles to upload only the
	 * quads that changed. */
	/* XXX change the renderer so that the ortho projection can be
	 * set up correctly depending on the actual window size. */

	surface = upload_layer (hr, layer);
	if (!surface) {
		VK_ERROR ("HR LAYER: can't upload layer to OpenGL, skipping");
		return;
	}
	vk_surface_draw (surface);
	vk_surface_destroy (&surface);
}

/****************************************************************************
 Interface
****************************************************************************/

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Fill in the debug stuff. */
	update_debug_flags (hr);
	hr->debug.current_mesh = 0;

	/* clear the frame buffer to a bright pink color */
	glClearColor (1.0f, 0.0f, 1.0f, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Reset the modelview matrix */
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	/* DEBUG: Draw the FB. */
	if (hr->debug.flags & HR_DEBUG_DRAW_FB) {
		if (!hr->textures.fb)
			hr->textures.fb =
				vk_surface_new (2048, 2048, VK_SURFACE_FORMAT_RGBA5551);
		if (!hr->textures.fb) {
			VK_ERROR ("HR: failed to create FB surface, turning off HR_DRAW_FB");
			hr->debug.flags &= ~HR_DEBUG_DRAW_FB;
		} else {
			upload_fb (hr);
			vk_surface_draw (hr->textures.fb);
		}
	}

	/* DEBUG: Draw the TEXRAM. */
	if (hr->debug.flags & HR_DEBUG_DRAW_TEXRAM) {
		if (!hr->textures.texram)
			hr->textures.texram =
				vk_surface_new (2048, 2048, VK_SURFACE_FORMAT_RGBA5551);
		if (!hr->textures.texram) {
			VK_ERROR ("HR: failed to create TEXRAM surface, turning off HR_DRAW_TEXRAM");
			hr->debug.flags &= ~HR_DEBUG_DRAW_TEXRAM;
		} else {
			upload_texram (hr);
			vk_surface_draw (hr->textures.texram);
		}
	}
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	(void) renderer;
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	clear_texture_cache (hr);
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;

		vk_surface_destroy (&hr->textures.debug);
		vk_surface_destroy (&hr->textures.fb);
		vk_surface_destroy (&hr->textures.texram);

		clear_texture_cache (hr);
	}
}

static vk_surface_t *
build_debug_surface (void)
{
	/* Build a colorful 2x2 checkerboard surface */
	vk_surface_t *surface = vk_surface_new (2, 2, VK_SURFACE_FORMAT_RGBA4444);
	if (!surface)
		return NULL;
	vk_surface_put16 (surface, 0, 0, 0xF000);
	vk_surface_put16 (surface, 0, 1, 0x0F00);
	vk_surface_put16 (surface, 1, 0, 0x00F0);
	vk_surface_put16 (surface, 1, 1, 0xFFF0);
	vk_surface_commit (surface);
	return surface;
}

static bool
build_default_surfaces (hikaru_renderer_t *hr)
{
	hr->textures.debug = build_debug_surface ();
	if (!hr->textures.debug)
		return false;

	return true;
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

	/* Read options from the environment */
	read_debug_flags (hr);

	/* Create a few surfaces */
	if (!build_default_surfaces (hr))
		goto fail;

	clear_texture_cache (hr);

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
