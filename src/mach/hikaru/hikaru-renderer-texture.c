/* 
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
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

#include "mach/hikaru/hikaru-renderer-private.h"

static void
get_sizes (uint32_t *w, uint32_t *h, hikaru_texhead_t *th)
{
	*w = 16 << th->logw;
	*h = 16 << th->logh;
}

static void
get_wrap_modes (int *wrap_u, int *wrap_v, hikaru_texhead_t *th)
{
	*wrap_u = *wrap_v = -1;

	if (th->wrapu == 0)
		*wrap_u = GL_CLAMP;
	else if (th->repeatu == 0)
		*wrap_u = GL_REPEAT;
	else
		*wrap_u = GL_MIRRORED_REPEAT;

	if (th->wrapv == 0)
		*wrap_v = GL_CLAMP;
	else if (th->repeatv == 0)
		*wrap_v = GL_REPEAT;
	else
		*wrap_v = GL_MIRRORED_REPEAT;
}

uint32_t
abgr1111_to_rgba4444 (uint8_t pixel)
{
	static const uint32_t table[16] = {
		0x0000, 0xF000, 0x0F00, 0xFF00,
		0x00F0, 0xF0F0, 0x0FF0, 0xFFF0,
		0x000F, 0xF00F, 0x0F0F, 0xFF0F,
		0x00FF, 0xF0FF, 0x0FFF, 0xFFFF,
	};

	return table[pixel & 15];
}

uint16_t
abgr1555_to_rgba5551 (uint16_t c)
{
	uint16_t r, g, b, a;

	r = (c >>  0) & 0x1F;
	g = (c >>  5) & 0x1F;
	b = (c >> 10) & 0x1F;
	a = (c >> 15) & 1;

	return (r << 11) | (g << 6) | (b << 1) | a;
}

uint16_t
abgr4444_to_rgba4444 (uint16_t c)
{
	uint16_t r, g, b, a;

	r = (c >>  0) & 0xF;
	g = (c >>  4) & 0xF;
	b = (c >>  8) & 0xF;
	a = (c >> 12) & 0xF;

	return (r << 12) | (g << 8) | (b << 4) | a;
}

uint32_t
a8_to_rgba8888 (uint32_t a)
{
	a &= 0xFF;
	return (a << 24) | (a << 16) | (a << 8) | a;
}

static vk_surface_t *
decode_texhead_rgba1111 (hikaru_renderer_t *hr, hikaru_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->bank];
	uint32_t w, h, basex, basey, x, y;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	get_sizes (&w, &h, texhead);
	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	get_texhead_coords (&basex, &basey, texhead); 

	surface = vk_surface_new (w, h*2, VK_SURFACE_FORMAT_RGBA4444, wrap_u, wrap_v);
	if (!surface)
		return NULL;

	for (y = 0; y < h; y ++) {
		for (x = 0; x < w; x += 4) {
			uint32_t offs = (basey + y) * 4096 + (basex + x);
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x + 0, y*2 + 0,
			                  abgr1111_to_rgba4444 (texels >> 28));
			vk_surface_put16 (surface, x + 1, y*2 + 0,
			                  abgr1111_to_rgba4444 (texels >> 24));
			vk_surface_put16 (surface, x + 0, y*2 + 1,
			                  abgr1111_to_rgba4444 (texels >> 20));
			vk_surface_put16 (surface, x + 1, y*2 + 1,
			                  abgr1111_to_rgba4444 (texels >> 16));
			vk_surface_put16 (surface, x + 2, y*2 + 0,
			                  abgr1111_to_rgba4444 (texels >> 12));
			vk_surface_put16 (surface, x + 3, y*2 + 0,
			                  abgr1111_to_rgba4444 (texels >>  8));
			vk_surface_put16 (surface, x + 2, y*2 + 1,
			                  abgr1111_to_rgba4444 (texels >>  4));
			vk_surface_put16 (surface, x + 3, y*2 + 1,
			                  abgr1111_to_rgba4444 (texels >>  0));
		}
	}
	return surface;
}

static vk_surface_t *
decode_texhead_abgr1555 (hikaru_renderer_t *hr, hikaru_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->bank];
	uint32_t w, h, basex, basey, x, y;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	get_sizes (&w, &h, texhead);
	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	get_texhead_coords (&basex, &basey, texhead); 

	surface = vk_surface_new (w, h, VK_SURFACE_FORMAT_RGBA5551, wrap_u, wrap_v);
	if (!surface)
		return NULL;

	for (y = 0; y < h; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < w; x += 2) {
			uint32_t offs  = base + x * 2;
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x+0, y, abgr1555_to_rgba5551 (texels >> 16));
			vk_surface_put16 (surface, x+1, y, abgr1555_to_rgba5551 (texels));
		}
	}
	return surface;
}

static vk_surface_t *
decode_texhead_abgr4444 (hikaru_renderer_t *hr, hikaru_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->bank];
	uint32_t w, h, basex, basey, x, y;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	get_sizes (&w, &h, texhead);
	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	get_texhead_coords (&basex, &basey, texhead); 

	surface = vk_surface_new (w, h, VK_SURFACE_FORMAT_RGBA4444, wrap_u, wrap_v);
	if (!surface)
		return NULL;

	for (y = 0; y < h; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < w; x += 2) {
			uint32_t offs  = base + x * 2;
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x+0, y, abgr4444_to_rgba4444 (texels >> 16));
			vk_surface_put16 (surface, x+1, y, abgr4444_to_rgba4444 (texels));
		}
	}
	return surface;
}

static vk_surface_t *
decode_texhead_a8 (hikaru_renderer_t *hr, hikaru_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->gpu->texram[texhead->bank];
	uint32_t w, h, basex, basey, x, y;
	int wrap_u, wrap_v;
	vk_surface_t *surface;

	get_sizes (&w, &h, texhead);
	get_wrap_modes (&wrap_u, &wrap_v, texhead);
	get_texhead_coords (&basex, &basey, texhead); 

	w /= 4;
	h /= 2;

	surface = vk_surface_new (w * 4, h, VK_SURFACE_FORMAT_RGBA8888, wrap_u, wrap_v);
	if (!surface)
		return NULL;

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
	hikaru_texhead_t	texhead;
	vk_surface_t		*surface;
} texcache[2][0x40][0x80];
static bool is_texcache_clear[2] = { false, false };

static bool
is_texhead_eq (hikaru_renderer_t *hr,
               hikaru_texhead_t *a, hikaru_texhead_t *b)
{
	return (a->format == b->format) &&
	       (a->logw == b->logw) &&
	       (a->logh == b->logh) &&
	       (a->bank == b->bank) &&
	       (a->slotx == b->slotx) &&
	       (a->sloty == b->sloty);
}

static void
dump_texhead (hikaru_renderer_t *hr,
              hikaru_texhead_t *texhead,
              vk_surface_t *surface)
{
	static unsigned num = 0;

	vk_machine_t *mach = ((vk_device_t *) hr->gpu)->mach;
	char path[256];
	FILE *fp;

	sprintf (path, "texheads/%s-texhead%u-%02X-%02X-%ux%u-%u.bin",
	         mach->game->name, num,
	         texhead->slotx, texhead->sloty,
	         16 << texhead->logw, 16 << texhead->logh,
	         texhead->format);
	fp = fopen (path, "wb");
	if (!fp)
		return;

	vk_surface_dump (surface, path);

	fclose (fp);
	num += 1;
}

vk_surface_t *
hikaru_renderer_decode_texture (vk_renderer_t *rend,
                                hikaru_texhead_t *texhead)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	hikaru_texhead_t *cached;
	vk_surface_t *surface = NULL;
	uint32_t bank, slotx, sloty, realx, realy;

	VK_ASSERT (hr);

	bank  = texhead->bank;
	slotx = texhead->slotx;
	sloty = texhead->sloty;

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

void
hikaru_renderer_invalidate_texcache (vk_renderer_t *rend,
                                     hikaru_texhead_t *th)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;

	VK_ASSERT (hr);

	if (th == NULL) {
		/* Clear everything. */
		clear_texture_cache_bank (hr, 0);
		clear_texture_cache_bank (hr, 1);
	} else {
		/* Simplest approach possible, clear the whole bank. */
		clear_texture_cache_bank (hr, th->bank);
	}
}
