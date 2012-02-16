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
                          vec3f_t *v0,
                          vec3f_t *v1,
                          vec3f_t *v2,
                          vec2f_t *uv0,
                          vec2f_t *uv1,
                          vec2f_t *uv2)
{
	glBegin (GL_TRIANGLES);
		glTexCoord2fv (uv0->x);
		glVertex3fv (v0->x);
		glTexCoord2fv (uv1->x);
		glVertex3fv (v1->x);
		glTexCoord2fv (uv2->x);
		glVertex3fv (v2->x);
	glEnd ();
}

static void
bind_ascii_texture (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	unsigned x, y;

	/* Upload the RGBA444 data in the bootrom defined ASCII texture */
	for (y = 0; y < 64; y++)
		for (x = 0; x < 128; x++) {
			/* XXX should be 8192 instead of 4096? */
			uint32_t i = x*2 + y*0x1000 + 0xF00;
			uint16_t c = vk_buffer_get (hr->texram, 2, i);
			vk_surface_put32 (hr->texture, x, y, c);
		}

	vk_surface_clear (hr->texture);
	vk_surface_commit (hr->texture);
}

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	glEnable (GL_TEXTURE_2D);
	bind_ascii_texture (renderer);
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
		hr->texture = vk_surface_new (128, 64, GL_RGBA4);
	}
	return (vk_renderer_t *) hr;
}
