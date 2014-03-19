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

/* TODO implement dirty rectangles. */

static void
draw_layer (hikaru_renderer_t *hr, hikaru_layer_t *layer)
{
	vk_buffer_t *fb = hr->gpu->fb;
	void *data = vk_buffer_get_ptr (fb, layer->y0 * 4096 + layer->x0 * 4);
	GLuint id;

	glGenTextures (1, &id);
	VK_ASSERT (id);

	glBindTexture (GL_TEXTURE_2D, id);

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	switch (layer->format) {
	case HIKARU_FORMAT_ABGR1555:
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 2048);

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGB5_A1,
		              640, 480, 0,
		              GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
		              data);
		break;
	case HIKARU_FORMAT_A2BGR10:
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 1024);

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGBA8,
		              640, 480, 0,
		              GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV,
		              data);
		break;
	default:
		VK_ASSERT (0);
	}
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei (GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei (GL_UNPACK_SKIP_PIXELS, 0);

	LOG ("drawing LAYER %s", get_layer_str (layer));

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

