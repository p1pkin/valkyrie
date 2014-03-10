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

#include "mach/hikaru/hikaru-renderer-private.h"

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
decode_layer_argb1555 (hikaru_renderer_t *hr, hikaru_layer_t *layer)
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
decode_layer_argb8888 (hikaru_renderer_t *hr, hikaru_layer_t *layer)
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
draw_layer (hikaru_renderer_t *hr, hikaru_layer_t *layer)
{
	vk_surface_t *surface;

	VK_ASSERT (hr);
	VK_ASSERT (layer);

	/* XXX cache the layers and use uploaded rectangles to upload only the
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

	LOG ("drawing LAYER %s", get_layer_str (layer));

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

void
hikaru_renderer_draw_layers (hikaru_renderer_t *hr, bool background)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_layer_t *layer;

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

