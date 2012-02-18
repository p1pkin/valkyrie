/* 
 * Valkyrie
 * Copyright (C) 2011, Stefano Teso
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "mach/hikaru/hikaru-renderer.h"

void
hikaru_renderer_draw_tri (hikaru_renderer_t *renderer,
                          vec3f_t *v0, vec3f_t *v1, vec3f_t *v2,
                          bool has_color,
                          vec4b_t color,
                          bool has_texture,
                          vec2s_t *uv0, vec2s_t *uv1, vec2s_t *uv2)
{
	VK_LOG ("TRI: CE=%u TE=%u", has_color, has_texture);
#if 0
	if (has_color) {
		glDisable (GL_TEXTURE_2D);
		glColor3b (color.x[0], color.x[1], color.x[2]); // color.x[3]);
	} else {
		glEnable (GL_TEXTURE_2D);
	}
#endif
	glBegin (GL_TRIANGLES);
		glTexCoord2s (uv0->x[0], uv0->x[1]);
		glVertex3fv (v0->x);
		glTexCoord2s (uv1->x[0], uv1->x[1]);
		glVertex3fv (v1->x);
		glTexCoord2s (uv2->x[0], uv2->x[1]);
		glVertex3fv (v2->x);
	glEnd ();
}

static inline uint32_t
rgba4_to_rgba8 (uint32_t p)
{
	return ((p & 0x000F) <<  4) |
	       ((p & 0x00F0) <<  8) |
	       ((p & 0x0F00) << 12) |
	       ((p & 0xF000) << 16);
}

static void
bind_texram (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	unsigned x, y;

	/* Upload the RGBA4444 data in the bootrom defined ASCII texture */
	for (y = 0; y < 2048; y++) {
		/* Each row is 2048 16bpp texels */
		uint32_t yoffs = y * 2048 * 2;
		for (x = 0; x < 2048; x++) {
			uint32_t offs = yoffs + x * 2;
			uint32_t texel = rgba4_to_rgba8 (vk_buffer_get (hr->texram, 2, offs));
			/* Note: the xor here is needed; is the GPU a big
			 * endian device? */
			vk_surface_put32 (hr->texture, x ^ 1, y, texel);
		}
	}

	vk_surface_commit (hr->texture);

	/* The ASCII texture is at [1920,2048)x[0,64) in TEXRAM */
	/* A character is 8x8 pixels; the entire ASCII table is 16x8 tiles */
#if 0
	glBegin (GL_TRIANGLE_STRIP);
		glTexCoord2s (1920, 0);
		glVertex3f (0.0f, 0.0f, 0.1f);
		glTexCoord2s (2048, 0);
		glVertex3f (639.0f, 0.0f, 0.1f);
		glTexCoord2s (1920, 64);
		glVertex3f (0.0f, 479.0f, 0.1f);
		glTexCoord2s (2048, 64);
		glVertex3f (639.0f, 479.0f, 0.1f);
	glEnd ();
#endif
}

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	glEnable (GL_TEXTURE_2D);
	glMatrixMode (GL_TEXTURE);
	glLoadIdentity ();
	glScalef (1.0f/2048, 1.0f/2048, 1.0f);
	bind_texram (renderer);
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	glDisable (GL_TEXTURE_2D);
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	(void) renderer;
}

static void
hikaru_renderer_delete (vk_renderer_t **renderer_)
{
	(void) renderer_;
}

vk_renderer_t *
hikaru_renderer_new (vk_buffer_t *texram)
{
	hikaru_renderer_t *hr = ALLOC (hikaru_renderer_t);
	if (hr) {
		/* setup other stuff here, including programs */

		hr->base.width = 640;
		hr->base.height = 480;

		hr->base.delete = hikaru_renderer_delete;
		hr->base.reset = hikaru_renderer_reset;
		hr->base.begin_frame = hikaru_renderer_begin_frame;
		hr->base.end_frame = hikaru_renderer_end_frame;

		/* XXX handle the return value */
		vk_renderer_init ((vk_renderer_t *) hr);

		hr->texram = texram;

		/* XXX handle the return value */
		hr->texture = vk_surface_new (2048, 2048, GL_RGBA8);
	}
	return (vk_renderer_t *) hr;
}
