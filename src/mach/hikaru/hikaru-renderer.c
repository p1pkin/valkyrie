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
#include "vk/surface.h"

/* XXX ditch immediate mode; use VBOs instead. */
/* XXX ditch the fixed pipeline; use shaders instead. */
/* XXX */
/* XXX use the IDMA device to detect where textures are; possibly in
 * slave RAM */

typedef struct {
	vec3f_t pos;		/* 12 bytes */
	vec3f_t normal;		/* 12 bytes */
	vec2f_t texcoords;	/* 8 bytes */
} hikaru_vertex_t;

typedef struct {
	vk_renderer_t base;

	/* Texture data */
	vk_buffer_t *texram;
	vk_surface_t *texture;

	/* Model data */
	hikaru_vertex_t	*vbo;
	int vbo_index;

} hikaru_renderer_t;

/* 3D Rendering */

#if 0
/* XXX PORTME */
void
hikaru_renderer_draw_tri (hikaru_renderer_t *renderer,
                          vec3f_t *v0, vec3f_t *v1, vec3f_t *v2,
                          bool has_color,
                          vec3b_t color,
                          bool has_texture,
                          vec2s_t *uv0, vec2s_t *uv1, vec2s_t *uv2)
{
	glBegin (GL_TRIANGLES);
		glTexCoord2s (uv0->x[0], uv0->x[1]);
		glVertex3fv (v0->x);
		glTexCoord2s (uv1->x[0], uv1->x[1]);
		glVertex3fv (v1->x);
		glTexCoord2s (uv2->x[0], uv2->x[1]);
		glVertex3fv (v2->x);
	glEnd ();
}

static int
get_vertex_index (int i)
{
	return (i < 0) ? (i + 3) : i;
}

static void
draw_tri (hikaru_gpu_t *gpu, vec2s_t *uv0, vec2s_t *uv1, vec2s_t *uv2)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) gpu->base.mach->renderer;

	int i0 = get_vertex_index (gpu->vertex_index - 1);
	int i1 = get_vertex_index (gpu->vertex_index - 2);
	int i2 = get_vertex_index (gpu->vertex_index - 3);

	hikaru_renderer_draw_tri (hr,
	                          &gpu->vertex_buffer[i0],
	                          &gpu->vertex_buffer[i1],
	                          &gpu->vertex_buffer[i2],
	                          true,
	                          gpu->current_ms->color[1],
	                          gpu->current_ms->has_texture,
	                          uv0, uv1, uv2);
}

static void
append_vertex (hikaru_renderer_t, vec3f_t *pos)
{
	gpu->vertex_buffer[gpu->vertex_index] = *src;
	gpu->vertex_buffer[gpu->vertex_index].x[1] += 480.0f; /* XXX hack */
	gpu->vertex_index = (gpu->vertex_index + 1) % 3;
}
#endif

void
hikaru_renderer_set_viewport (vk_renderer_t *renderer,
                              hikaru_gpu_viewport_t *viewport)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	(void) hr;
}

void
hikaru_renderer_set_matrix (vk_renderer_t *renderer, mtx4x4f_t *mtx)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	(void) hr;
}

void
hikaru_renderer_set_material (vk_renderer_t *renderer,
                              hikaru_gpu_material_t *material)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	(void) hr;
}

void
hikaru_renderer_set_texhead (vk_renderer_t *renderer,
                             hikaru_gpu_texhead_t *texhead)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	(void) hr;
}

void
hikaru_renderer_set_light (vk_renderer_t *renderer,
                           hikaru_gpu_light_t *light)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	(void) hr;
}

static void
draw_vbo (hikaru_renderer_t *hr)
{
}

static void
begin_vbo (hikaru_renderer_t *hr)
{
	if (hr->vbo_index < 0) {
		/* XXX lookup existing VBO or generate a new one. Return
		 * true if a match was found. */
		hr->vbo_index = 0;
	}
}

static void
end_vbo (hikaru_renderer_t *hr)
{
	if (hr->vbo_index >= 0) {
		/* XXX upload the VBO; draw it */
		draw_vbo (hr);
		hr->vbo_index = -1;
	}
}

void
hikaru_renderer_append_vertex_full (vk_renderer_t *renderer,
                                    vec3f_t *pos,
                                    vec3f_t *normal,
                                    vec2f_t *texcoords)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	if (hr->vbo_index < 0)
		begin_vbo (hr);
	hr->vbo[hr->vbo_index].pos = *pos;
	hr->vbo[hr->vbo_index].normal = *normal;
	hr->vbo[hr->vbo_index].texcoords = *texcoords;
}

void
hikaru_renderer_append_vertex (vk_renderer_t *renderer, vec3f_t *pos)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	if (hr->vbo_index < 0)
		begin_vbo (hr);
	hr->vbo[hr->vbo_index].pos = *pos;
}

void
hikaru_renderer_append_texcoords (vk_renderer_t *renderer,
                                  vec2f_t texcoords[3])
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	VK_ASSERT (hr->vbo_index >= 2);

	hr->vbo[hr->vbo_index-2].texcoords = texcoords[0];
	hr->vbo[hr->vbo_index-1].texcoords = texcoords[1];
	hr->vbo[hr->vbo_index-0].texcoords = texcoords[2];
}

void
hikaru_renderer_end_vertex_data (vk_renderer_t *renderer)
{
	end_vbo ((hikaru_renderer_t *) renderer);
}

/* 2D Rendering and Texture Decoding/Upload */

void
hikaru_renderer_draw_layer (vk_renderer_t *renderer,
                            vec2i_t coords[2])
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Note: the Y axis is facing upwards by default. */
	glBegin (GL_TRIANGLE_STRIP);
		glTexCoord2s (coords[0].x[0], coords[0].x[1]);
		glVertex3f (0.0f, 479.0f, 0.1f);
		glTexCoord2s (coords[1].x[0], coords[0].x[1]);
		glVertex3f (639.0f, 479.0f, 0.1f);
		glTexCoord2s (coords[0].x[0], coords[1].x[1]);
		glVertex3f (0.0f, 0.0f, 0.1f);
		glTexCoord2s (coords[1].x[0], coords[1].x[1]);
		glVertex3f (639.0f, 0.0f, 0.1f);
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
bind_texram (hikaru_renderer_t *hr)
{
	unsigned x, y;

	/* XXX this is _very_ slow; it's one of the main CPU hogs in
	 * valkyrie. */

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
}

/* Interface */

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Set the texture matrix */
	glEnable (GL_TEXTURE_2D);
	glMatrixMode (GL_TEXTURE);
	glLoadIdentity ();
	glScalef (1.0f/2048, 1.0f/2048, 1.0f);

	/* Upload the TEXRAM data */
	bind_texram (hr);

	/* Reset the temporary vertex data */
	hr->vbo_index = -1;
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	(void) renderer;
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	(void) renderer;
}

static void
hikaru_renderer_delete (vk_renderer_t **renderer_)
{
	if (renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;
		vk_surface_delete (&hr->texture);
	}
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
