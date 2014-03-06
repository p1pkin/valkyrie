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
#include <GL/glew.h>
#include <SDL/SDL.h>

#include "vk/input.h"

#include "mach/hikaru/hikaru-renderer.h"
#include "mach/hikaru/hikaru-gpu-private.h"

/****************************************************************************
 Debug
****************************************************************************/

enum {
	HR_DEBUG_LOG,
	HR_DEBUG_NO_LAYER1,
	HR_DEBUG_NO_LAYER2,
	HR_DEBUG_NO_3D,
	HR_DEBUG_SELECT_CULLFACE,
	HR_DEBUG_NO_TEXTURES,
	HR_DEBUG_USE_DEBUG_TEXTURE,
	HR_DEBUG_DUMP_TEXTURES,
	HR_DEBUG_SELECT_POLYTYPE,
	HR_DEBUG_DRAW_NORMALS,
	HR_DEBUG_NO_LIGHTING,
	HR_DEBUG_NO_AMBIENT,
	HR_DEBUG_NO_DIFFUSE,
	HR_DEBUG_NO_SPECULAR,
	HR_DEBUG_SELECT_ATT_TYPE,

	HR_NUM_DEBUG_VARS
};

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
	[HR_DEBUG_SELECT_CULLFACE]	= { -1, 1, SDLK_c,  true, "SELECT CULLFACE" },
	[HR_DEBUG_NO_TEXTURES]		= {  0, 1, SDLK_t, false, "NO TEXTURES" },
	[HR_DEBUG_USE_DEBUG_TEXTURE]	= {  0, 1, SDLK_y, false, "USE DEBUG TEXTURE" },
	[HR_DEBUG_DUMP_TEXTURES]	= {  0, 1,     ~0, false, "DUMP TEXTURES" },
	[HR_DEBUG_SELECT_POLYTYPE]	= { -1, 7, SDLK_p,  true, "SELECT POLYTYPE" },
	[HR_DEBUG_DRAW_NORMALS]		= {  0, 1, SDLK_n, false, "DRAW NORMALS" },
	[HR_DEBUG_NO_LIGHTING]		= {  0, 1, SDLK_l, false, "NO LIGHTING" },
	[HR_DEBUG_NO_AMBIENT]		= {  0, 1, SDLK_a, false, "NO AMBIENT" },
	[HR_DEBUG_NO_DIFFUSE]		= {  0, 1, SDLK_d, false, "NO DIFFUSE" },
	[HR_DEBUG_NO_SPECULAR]		= {  0, 1, SDLK_s, false, "NO SPECULAR" },
	[HR_DEBUG_SELECT_ATT_TYPE]	= { -1, 3, SDLK_z,  true, "SELECT ATT TYPE" },
};

static void
init_debug_flags (hikaru_renderer_t *hr)
{
	unsigned i;

	hr->debug.flags = (int32_t *) calloc (HR_NUM_DEBUG_VARS, sizeof (int32_t));
	VK_ASSERT (hr->debug.flags);

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

static void
destroy_debug_flags (hikaru_renderer_t *hr)
{
	free (hr->debug.flags);
}

#define LOG(fmt_, args_...) \
	do { \
		if (hr->debug.flags[HR_DEBUG_LOG]) \
			fprintf (stdout, "\tHR: " fmt_"\n", ##args_); \
	} while (0)

/****************************************************************************
 Texhead Decoding
****************************************************************************/

static void
get_wrap_modes (int *wrap_u, int *wrap_v, hikaru_gpu_texhead_t *th)
{
	*wrap_u = *wrap_v = -1;

	if (th->_2C1.wrapu == 0)
		*wrap_u = GL_CLAMP;
	else if (th->_2C1.repeatu == 0)
		*wrap_u = GL_REPEAT;
	else
		*wrap_u = GL_MIRRORED_REPEAT;

	if (th->_2C1.wrapv == 0)
		*wrap_v = GL_CLAMP;
	else if (th->_2C1.repeatv == 0)
		*wrap_v = GL_REPEAT;
	else
		*wrap_v = GL_MIRRORED_REPEAT;
}

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
	vk_buffer_t *texram = hr->gpu->texram[texhead->_4C1.bank];
	uint32_t basex, basey, x, y;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	surface = vk_surface_new (texhead->width, texhead->height*2,
	                          VK_SURFACE_FORMAT_RGBA4444, wrap_u, wrap_v);
	if (!surface)
		return NULL;

	get_texhead_coords (&basex, &basey, texhead); 

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
	vk_buffer_t *texram = hr->gpu->texram[texhead->_4C1.bank];
	uint32_t basex, basey, x, y;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	surface = vk_surface_new (texhead->width, texhead->height,
	                          VK_SURFACE_FORMAT_RGBA5551, wrap_u, wrap_v);
	if (!surface)
		return NULL;

	get_texhead_coords (&basex, &basey, texhead); 

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
	vk_buffer_t *texram = hr->gpu->texram[texhead->_4C1.bank];
	uint32_t basex, basey, x, y;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	surface = vk_surface_new (texhead->width, texhead->height,
	                          VK_SURFACE_FORMAT_RGBA4444, wrap_u, wrap_v);
	if (!surface)
		return NULL;

	get_texhead_coords (&basex, &basey, texhead); 

	for (y = 0; y < texhead->height; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < texhead->width; x += 2) {
			uint32_t offs  = base + x * 2;
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x+0, y, abgr4444_to_rgba4444 (texels >> 16));
			vk_surface_put16 (surface, x+1, y, abgr4444_to_rgba4444 (texels));
		}
	}
	return surface;
}

static uint32_t
a8_to_rgba8888 (uint32_t a)
{
	a &= 0xFF;
	return (a << 24) | (a << 16) | (a << 8) | a;
}

static vk_surface_t *
decode_texhead_a8 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->_4C1.bank];
	uint32_t basex, basey, x, y, h, w;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	h = texhead->height / 2;
	w = texhead->width / 4;

	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	surface = vk_surface_new (w * 4, h, VK_SURFACE_FORMAT_RGBA8888, wrap_u, wrap_v);
	if (!surface)
		return NULL;

	get_texhead_coords (&basex, &basey, texhead); 

	for (y = 0; y < h; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < w; x++) {
			uint32_t offs = base + x * 4;
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put32 (surface, 4*x+0, y, a8_to_rgba8888 (texels >> 24));
			vk_surface_put32 (surface, 4*x+1, y, a8_to_rgba8888 (texels >> 16));
			vk_surface_put32 (surface, 4*x+2, y, a8_to_rgba8888 (texels >> 8));
			vk_surface_put32 (surface, 4*x+3, y, a8_to_rgba8888 (texels));
		}
	}
	return surface;
}

static struct {
	hikaru_gpu_texhead_t	texhead;
	vk_surface_t		*surface;
} texcache[2][0x40][0x80];
static bool is_texcache_clear[2] = { false, false };

static bool
is_texhead_eq (hikaru_renderer_t *hr,
               hikaru_gpu_texhead_t *a, hikaru_gpu_texhead_t *b)
{
	return (a->_2C1.format == b->_2C1.format) &&
	       (a->_4C1.bank == b->_4C1.bank) &&
	       (a->_4C1.slotx == b->_4C1.slotx) &&
	       (a->_4C1.sloty == b->_4C1.sloty) &&
	       (a->width == b->width) &&
	       (a->height == b->height);
}

static void
dump_texhead (hikaru_renderer_t *hr,
              hikaru_gpu_texhead_t *texhead,
              vk_surface_t *surface)
{
	static unsigned num = 0;

	vk_machine_t *mach = ((vk_device_t *) hr->gpu)->mach;
	char path[256];
	FILE *fp;

	sprintf (path, "texheads/%s-texhead%u-%02X-%02X-%ux%u-%u.bin",
	         mach->game->name, num,
	         texhead->_4C1.slotx, texhead->_4C1.sloty,
	         texhead->width, texhead->height,
	         texhead->_2C1.format);
	fp = fopen (path, "wb");
	if (!fp)
		return;

	vk_surface_dump (surface, path);

	fclose (fp);
	num += 1;
}

static vk_surface_t *
decode_texhead (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	hikaru_gpu_texhead_t *cached;
	vk_surface_t *surface = NULL;
	uint32_t bank, slotx, sloty, realx, realy;

	bank  = texhead->_4C1.bank;
	slotx = texhead->_4C1.slotx;
	sloty = texhead->_4C1.sloty;

	/* Handle invalid slots here. */
	if (slotx < 0x80 || sloty < 0xC0)
		return NULL;

	realx = slotx - 0x80;
	realy = sloty - 0xC0;

	/* Lookup the texhead in the cache. */
	cached = &texcache[bank][realy][realx].texhead;
	if (is_texhead_eq (hr, texhead, cached)) {
		surface = texcache[bank][realy][realx].surface;
		if (surface)
			return surface;
	}

	/* Texhead not cached, decode it. */
	switch (texhead->_2C1.format) {
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

	if (surface && hr->debug.flags[HR_DEBUG_DUMP_TEXTURES])
		dump_texhead (hr, texhead, surface);

	/* Cache the decoded texhead. */
	texcache[bank][realy][realx].texhead = *texhead;
	texcache[bank][realy][realx].surface = surface;
	is_texcache_clear[bank] = false;

	/* Upload the surface to the GL. */
	vk_surface_commit (surface);
	return surface;
}

static void
clear_texture_cache_bank (hikaru_renderer_t *hr, unsigned bank)
{
	unsigned x, y;

	if (is_texcache_clear[bank])
		return;
	is_texcache_clear[bank] = true;

	/* Free all allocated surfaces. */
	for (y = 0; y < 0x40; y++)
		for (x = 0; x < 0x80; x++)
			vk_surface_destroy (&texcache[bank][y][x].surface);

	/* Zero out the cache itself, to avoid spurious hits. Note that
	 * texture RAM origin is (80,C0), so (slotx, sloty) will never match
	 * a zeroed out cache entries. */
	memset ((void *) &texcache[bank], 0, sizeof (texcache[bank]));
}

static void
clear_texture_cache (hikaru_renderer_t *hr)
{
	clear_texture_cache_bank (hr, 0);
	clear_texture_cache_bank (hr, 1);
}

void
hikaru_renderer_invalidate_texcache (hikaru_renderer_t *hr,
                                     hikaru_gpu_texhead_t *th)
{
	VK_ASSERT (hr);
	VK_ASSERT (th);

	/* Simplest approach possible, clear the whole bank. */
	clear_texture_cache_bank (hr, th->_4C1.bank);
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

static void
upload_current_viewport (hikaru_renderer_t *hr)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_viewport_t *vp = &VP.scratch;

	if (vp->flags & HIKARU_GPU_OBJ_DIRTY) {

		const float h = vp->clip.t - vp->clip.b;
		const float w = vp->clip.r - vp->clip.l;
		const float hh_at_n = (h / 2.0f) * (vp->clip.n / vp->clip.f);
		const float hw_at_n = hh_at_n * (w / h);
		const float dcx = vp->offset.x - (w / 2.0f);
		const float dcy = vp->offset.y - (h / 2.0f);

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

		vp->flags &= ~HIKARU_GPU_OBJ_DIRTY;
	}
}

/* TODO track dirty state */
static void
upload_current_modelview (hikaru_renderer_t *hr, unsigned i)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_modelview_t *mv = &MV.table[i];

	LOG ("mv  = %s", get_gpu_modelview_str (mv));

	glMatrixMode (GL_MODELVIEW);
	glLoadMatrixf ((GLfloat *) &mv->mtx[0][0]);
}

/* TODO track dirty state */
static void
upload_current_material_texhead (hikaru_renderer_t *hr)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_material_t *mat = &MAT.scratch;
	hikaru_gpu_texhead_t *th   = &TEX.scratch;

	LOG ("mat = %s", get_gpu_material_str (mat));
	if (mat->set && mat->has_texture)
		LOG ("th  = %s", get_gpu_texhead_str (th));

	if (hr->debug.flags[HR_DEBUG_NO_TEXTURES] ||
	    !mat->set || !th->set || !mat->has_texture)
		glDisable (GL_TEXTURE_2D);
	else {
		vk_surface_t *surface;

		surface = hr->debug.flags[HR_DEBUG_USE_DEBUG_TEXTURE] ?
		          NULL : decode_texhead (hr, th);

		if (!surface)
			surface = hr->textures.debug;

		vk_surface_bind (surface);
		glEnable (GL_TEXTURE_2D);
	}
}

#define INV255	(1.0f / 255.0f)

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

/* TODO track dirty state */
static void
upload_current_lightset (hikaru_renderer_t *hr)
{
	static const float k = 1.0f / 255.0f;

	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_material_t *mat = &MAT.scratch;
	hikaru_gpu_lightset_t *ls = &LIT.scratchset;
	GLfloat tmp[4];
	unsigned i, n;

	if (hr->debug.flags[HR_DEBUG_NO_LIGHTING])
		goto disable;

	if (!ls->set) {
		VK_ERROR ("attempting to use unset lightset!");
		goto disable;
	}

	if (ls->disabled == 0xF) {
		VK_ERROR ("attempting to use lightset with no light!");
		goto disable;
	}

	/* If the material is unset, treat it as shading_mode is 1; that way
	 * we can actually check lighting in the viewer. */
	if (mat->set && mat->shading_mode == 0)
		goto disable;

	/* Lights are positioned according to the scene, irrespective of
	 * the modelview matrix. */
	glMatrixMode (GL_MODELVIEW);
	glPushMatrix ();
	glLoadIdentity ();

	glEnable (GL_LIGHTING);

	/* Set the global ambient. */
	get_light_ambient (hr, tmp);
	glLightModelf (GL_LIGHT_MODEL_TWO_SIDE, 1.0f);
	glLightModelfv (GL_LIGHT_MODEL_AMBIENT, tmp);

	/* For each of the four lights in the current lightset */
	for (i = 0; i < 4; i++) {
		hikaru_gpu_light_t *lt;

		if (ls->disabled & (1 << i))
			continue;

		lt = &LIT.table[ls->index[i]];

		if (!lt->set) {
			VK_ERROR ("attempting to use unset light %u!", ls->index[i]);
			continue;
		}

		LOG ("light%u = enabled, %s", i, get_gpu_light_str (lt));

		n = GL_LIGHT0 + i;

		if ((hr->debug.flags[HR_DEBUG_SELECT_ATT_TYPE] < 0) ||
		    (hr->debug.flags[HR_DEBUG_SELECT_ATT_TYPE] == lt->type))
			glEnable (n);
		else
			glDisable (n);

		/* The ambient light is only global */
		tmp[0] = tmp[1] = tmp[2] = 0.0f;
		tmp[3] = 1.0f;

		glLightfv (n, GL_AMBIENT, tmp);

		/* Set the diffuse color */
		/* XXX the index uploaded with 051 may be related to the
		 * table uploaded by 194, which may contain alpha values. */
		if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE])
			tmp[0] = tmp[1] = tmp[2] = 1.0f;
		else {
			tmp[0] = lt->diffuse[0] * k;
			tmp[1] = lt->diffuse[1] * k;
			tmp[2] = lt->diffuse[2] * k;
		}
		tmp[3] = 1.0f;

		glLightfv (n, GL_DIFFUSE, tmp);

		/* Set the specular color */
		/* XXX a relativey wild guess. */
		if (hr->debug.flags[HR_DEBUG_NO_SPECULAR])
			tmp[0] = tmp[1] = tmp[2] = 0.0f;
		else {
			tmp[0] = lt->specular[0] * k;
			tmp[1] = lt->specular[1] * k;
			tmp[2] = lt->specular[2] * k;
		}
		tmp[3] = 1.0f;

		glLightfv (n, GL_SPECULAR, tmp);

		/* Set the direction/position */
		if (lt->has_dir && !lt->has_pos) {
			/* directional light */
			tmp[0] = lt->dir[0];
			tmp[1] = lt->dir[1];
			tmp[2] = lt->dir[2];
			tmp[3] = 0.0f;
			glLightfv (n, GL_POSITION, tmp);
		} else if (!lt->has_dir && lt->has_pos) {
			/* point light */
			tmp[0] = lt->pos[0];
			tmp[1] = lt->pos[1];
			tmp[2] = lt->pos[2];
			tmp[3] = 1.0f;
			glLightfv (n, GL_POSITION, tmp);
		} else if (lt->has_dir && lt->has_pos) {
			/* spotlight */
			glPushMatrix ();
			glLoadIdentity ();

			tmp[0] = lt->dir[0];
			tmp[1] = lt->dir[1];
			tmp[2] = lt->dir[2];
			glTranslatef (tmp[0], tmp[1], tmp[2]);

			tmp[0] = lt->dir[0];
			tmp[1] = lt->dir[1];
			tmp[2] = lt->dir[2];
			tmp[3] = 1.0f;
			glLightfv (n, GL_POSITION, tmp);

			/* XXX let's make it very hard to miss spotlights! */
			glLightf (n, GL_SPOT_EXPONENT, 128.0f);

			glPopMatrix ();
		}

		/* Set the attenuation */
		/* XXX most attenuation types are impossible to do correctly
		 * with OpenGL fixed-function quadratic attenuation model. */
		if (lt->type == 0 &&
		    (lt->att_base == 1.0f && lt->att_offs == 1.0f)) {
			/* constant */

			tmp[0] = 1.0f;
			tmp[1] = 0.0f;
			tmp[2] = 0.0f;
		} else if (lt->type == 0) {
			/* linear */
			float min, max;

			VK_ASSERT (lt->att_base < 0.0f);
			VK_ASSERT (lt->att_offs < 0.0f);

			min = -lt->att_offs;
			max = min + 1.0f / lt->att_base;

			tmp[0] = 0.0f;
			tmp[1] = 1.0f / min;
			tmp[2] = 0.0f;
		} else if (lt->type == 1) {
			/* square */
			/* XXX used in BRAVEFF */

			tmp[0] = 0.0f;
			tmp[1] = 0.2f;
			tmp[3] = 0.0f;
		} else if (lt->type == 2) {
			/* reciprocal */
			VK_ASSERT (0);
		} else if (lt->type == 3) {
			/* reciprocal 2 */

			tmp[0] = 0.0f;
			tmp[1] = 0.2f;
			tmp[3] = 0.0f;
		} else
			VK_ASSERT (0);

		glLightf (n, GL_CONSTANT_ATTENUATION, tmp[0]);
		glLightf (n, GL_LINEAR_ATTENUATION, tmp[1]);
		glLightf (n, GL_QUADRATIC_ATTENUATION, tmp[2]);
	}

	/* We upload the material properties here, as we don't store all
	 * of them in the vertex_t yet (we will when we upgrade the renderer
	 * to GL 3.0 and GLSL). */

	/* Set the diffuse color */
	if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE]) {
		tmp[0] = tmp[1] = tmp[2] = 0.0f;
		tmp[3] = 1.0f;
	} else {
		tmp[0] = mat->diffuse[0] * k;
		tmp[1] = mat->diffuse[1] * k;
		tmp[2] = mat->diffuse[2] * k;
		tmp[3] = 1.0f;
	}

	glMaterialfv (GL_FRONT_AND_BACK, GL_DIFFUSE, tmp);

	/* Set the ambient color */
	if (hr->debug.flags[HR_DEBUG_NO_AMBIENT]) {
		tmp[0] = tmp[1] = tmp[2] = 0.0f;
		tmp[3] = 1.0f;
	} else {
		tmp[0] = mat->ambient[0] * k;
		tmp[1] = mat->ambient[1] * k;
		tmp[2] = mat->ambient[2] * k;
	}

	glMaterialfv (GL_FRONT_AND_BACK, GL_AMBIENT, tmp);

	/* Set the specular color */
	if (hr->debug.flags[HR_DEBUG_NO_SPECULAR]) {
		tmp[0] = tmp[1] = tmp[2] = tmp[3] = 0.0f;
	} else {
		tmp[0] = mat->specular[0] * k;
		tmp[1] = mat->specular[1] * k;
		tmp[2] = mat->specular[2] * k;
		tmp[3] = mat->specular[3] * k * 128.0f;
	}

	glMaterialfv (GL_FRONT_AND_BACK, GL_SPECULAR, tmp);
	glMaterialf (GL_FRONT_AND_BACK, GL_SHININESS, tmp[3]);

	glPopMatrix ();
	return;

disable:
	glDisable (GL_LIGHTING);
}

static void
draw_current_mesh (hikaru_renderer_t *hr)
{
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned num_instances = MV.total + 1, i;
	GLuint vbo;

	if (hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] >= 0 &&
	    hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] != POLY.type)
		return;

	LOG ("==== DRAWING MESH (#vertices=%u instances=%u) ====",
	     hr->mesh.num_pushed, num_instances);

	upload_current_viewport (hr);
	upload_current_material_texhead (hr);

	glGenBuffers (1, &vbo);
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glBufferData (GL_ARRAY_BUFFER,
	              sizeof (hikaru_gpu_vertex_t) * hr->mesh.num_tris * 3,
	              hr->mesh.vbo, GL_DYNAMIC_DRAW);

	glVertexPointer (3, GL_FLOAT, sizeof (hikaru_gpu_vertex_t),
	                 (const GLvoid *) offsetof (hikaru_gpu_vertex_t, pos));
	glNormalPointer (GL_FLOAT, sizeof (hikaru_gpu_vertex_t),
	                 (const GLvoid *) offsetof (hikaru_gpu_vertex_t, nrm));
//	glColorPointer (4, GL_FLOAT,  sizeof (hikaru_gpu_vertex_t),
//	                (const GLvoid *) offsetof (hikaru_gpu_vertex_t, col));
	glTexCoordPointer (2, GL_FLOAT,  sizeof (hikaru_gpu_vertex_t),
	                   (const GLvoid *) offsetof (hikaru_gpu_vertex_t, txc));

	glEnableClientState (GL_VERTEX_ARRAY);
	glEnableClientState (GL_NORMAL_ARRAY);
//	glEnableClientState (GL_COLOR_ARRAY);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);

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

	for (i = 0; i < num_instances; i++) {
		upload_current_modelview (hr, i);
		upload_current_lightset (hr);
		glDrawArrays (GL_TRIANGLES, 0, hr->mesh.num_tris * 3);
	}

	glDeleteBuffers (1, &vbo);

skip_:
	MV.total = 0;
	MV.depth = 0; /* XXX not really needed. */
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
	const float k = 1.0f / 255.0f;

	/* XXX at the moment we use only color 1 (it's responsible for the
	 * BOOTROM CRT test). */

	if (mat->set) {
		/* XXX check me! */
		dst->col[0] = mat->diffuse[0] * k;
		dst->col[1] = mat->diffuse[1] * k;
		dst->col[2] = mat->diffuse[2] * k;
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
		dst->col[3] = clampf (POLY.alpha + src->info.bit.alpha * k, 0.0f, 1.0f);
		break;
	}
}

static void
copy_texcoords (hikaru_renderer_t *hr,
                hikaru_gpu_vertex_t *dst, hikaru_gpu_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_texhead_t *th = &TEX.scratch;
	float height = th->height;

	if (th->_2C1.format == HIKARU_FORMAT_ABGR1111)
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
		uint32_t index = hr->mesh.num_tris * 3;
		hikaru_gpu_vertex_t *vbo = &hr->mesh.vbo[index];

		VK_ASSERT ((hr->mesh.num_tris*3+2) < MAX_VERTICES_PER_MESH);

		if (VTX (2).info.bit.winding) {
			vbo[0] = VTX(0);
			vbo[1] = VTX(2);
			vbo[2] = VTX(1);
		} else {
			vbo[0] = VTX(0);
			vbo[1] = VTX(1);
			vbo[2] = VTX(2);
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
		if (push & HR_PUSH_NRM) {
			VK_COPY_VEC3F (VTX(2).nrm, v->nrm);
			if (hr->debug.flags[HR_DEBUG_DRAW_NORMALS]) {
				VTX(2).col[0] = (v->nrm[0] * 0.5f) + 0.5f;
				VTX(2).col[1] = (v->nrm[1] * 0.5f) + 0.5f;
				VTX(2).col[2] = (v->nrm[2] * 0.5f) + 0.5f;
			}
		}

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
	if (v[0].info.bit.tricap == 7) {
		VTX(2).info.full = v[0].info.full;
		add_triangle (hr);
	}
}

static void
clear_mesh_data (hikaru_renderer_t *hr)
{
	memset ((void *) &hr->mesh.vtx, 0, sizeof (hr->mesh.vtx));
	hr->mesh.num_pushed = 0;
	hr->mesh.num_tris = 0;
	hr->mesh.addr[0] = ~0;
	hr->mesh.addr[1] = ~0;
}

void
hikaru_renderer_begin_mesh (hikaru_renderer_t *hr, uint32_t addr,
                            bool is_static)
{
	VK_ASSERT (hr);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	clear_mesh_data (hr);
	hr->mesh.addr[0] = addr;
}

void
hikaru_renderer_end_mesh (hikaru_renderer_t *hr, uint32_t addr)
{
	VK_ASSERT (hr);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	hr->mesh.addr[1] = addr;
	draw_current_mesh (hr);

	clear_mesh_data (hr);
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

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA5551, -1, -1);
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

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA8888, -1, -1);
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

static void
draw_layer (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;

	VK_ASSERT (hr);
	VK_ASSERT (layer);

	/* XXX cache the layers and use dirty rectangles to upload only the
	 * quads that changed. */
	/* XXX change the renderer so that the ortho projection can be
	 * set up correctly depending on the actual window size. */

	if (layer->format == HIKARU_FORMAT_ABGR8888)
		surface = decode_layer_argb8888 (hr, layer);
	else
		surface = decode_layer_argb1555 (hr, layer);

	if (!surface) {
		VK_ERROR ("HR LAYER: can't decode layer, skipping");
		return;
	}

	LOG ("drawing LAYER %s", get_gpu_layer_str (layer));

	vk_surface_commit (surface);
	glBegin (GL_TRIANGLE_STRIP);
		glTexCoord2f (0.0f, 0.0f);
		glVertex3f (0.0f, 0.0f, 0.0f);
		glTexCoord2f (1.0f, 0.0f);
		glVertex3f (1.0f, 0.0f, 0.0f);
		glTexCoord2f (0.0f, 1.0f);
		glVertex3f (0.0f, 1.0f, 0.0f);
		glTexCoord2f (1.0f, 1.0f);
		glVertex3f (1.0f, 1.0f, 0.0f);
	glEnd ();
	vk_surface_destroy (&surface);
}

static void
draw_layers (hikaru_renderer_t *hr, bool background)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_gpu_layer_t *layer;

	if (!LAYERS.enabled)
		return;

	if (background)
		return;

	/* Setup 2D state. */
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glOrtho (0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f);

	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	glDisable (GL_CULL_FACE);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_LIGHTING);

	glColor3f (1.0f, 1.0f, 1.0f);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable (GL_TEXTURE_2D);

	/* Only draw unit 0 for now. I think unit 1 is there only for
	 * multi-monitor, which case we don't care about. */
	layer = &LAYERS.layer[0][1];
	if (!layer->enabled || !(hr->debug.flags[HR_DEBUG_NO_LAYER2]))
		draw_layer (hr, layer);

	layer = &LAYERS.layer[0][0];
	if (!layer->enabled || !(hr->debug.flags[HR_DEBUG_NO_LAYER1]))
		draw_layer (hr, layer);
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

	/* clear the frame buffer to a bright pink color */
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Reset the modelview matrix */
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	/* Draw the background layers. */
	draw_layers (hr, true);
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Draw the foreground layers. */
	draw_layers (hr, false);
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

		clear_texture_cache (hr);

		destroy_debug_flags (hr);
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

	init_debug_flags (hr);

	hr->textures.debug = build_debug_surface ();
	if (!hr->textures.debug)
		goto fail;

	clear_texture_cache (hr);

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
