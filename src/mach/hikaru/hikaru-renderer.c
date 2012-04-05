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

typedef struct {
	vec3f_t pos;		/* 12 bytes */
	vec3f_t normal;		/* 12 bytes */
	vec2f_t texcoords;	/* 8 bytes */
} hikaru_vertex_t;

typedef struct {
	hikaru_gpu_texture_t texture;
	GLuint id;
} hikaru_texture_t;

typedef struct {
	vk_renderer_t base;
	bool log;

	/* Texture data */
	vk_buffer_t *texram;
	vk_surface_t *texture;
	hikaru_texture_t textures[256][256];

	/* Model data */
	hikaru_vertex_t *models[64];
	hikaru_vertex_t	vertices[1024];
	int vertex_index;
	int model_index;
	int hack;

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

	if (hr->log) {
		VK_LOG ("HR == VIEWPORT ==");
		VK_LOG ("HR persp   : { %f, %f, %f }",
		        viewport->persp_x, viewport->persp_y,
		        viewport->persp_unk);
		VK_LOG ("HR extents : { %d, %d } { %d, %d } { %d, %d }",
		        viewport->center.x[0],
		        viewport->center.x[1],
		        viewport->extents_x.x[0],
		        viewport->extents_x.x[1],
		        viewport->extents_y.x[0],
		        viewport->extents_y.x[1]);
		VK_LOG ("HR 421     : type=%u { %f, %f }",
		        viewport->unk_func,
		        viewport->unk_n,
		        viewport->unk_b);
		VK_LOG ("HR depth   : type=%u ena=%u unk=%u mask=#%02X%02X%02X%02X density=%f bias=%f ",
		        viewport->depth_type,
		        viewport->depth_enabled,
		        viewport->depth_unk,
		        viewport->depth_mask.x[3],
		        viewport->depth_mask.x[2],
		        viewport->depth_mask.x[1],
		        viewport->depth_mask.x[0],
		        viewport->depth_density,
		        viewport->depth_bias);
		VK_LOG ("HR ambient : RGB#%02X%02X%02X",
		        viewport->ambient_color.x[2],
		        viewport->ambient_color.x[1],
		        viewport->ambient_color.x[0]);
		VK_LOG ("HR clear   : RGBA#%02X%02X%02X%02X",
		        viewport->clear_color.x[3],
		        viewport->clear_color.x[2],
		        viewport->clear_color.x[1],
		        viewport->clear_color.x[0]);
	}

#if 0
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glFrustum (-viewport->persp_x / 2.0f,	/* left */
	           viewport->persp_x / 2.0f,	/* right */
	           -viewport->persp_y / 2.0f,	/* bottom */
	           viewport->persp_y / 2.0f,	/* top */
	           0.0f,			/* near */
	           100.0f);			/* far */

	glScissor (viewport->extents_x.x[0],
	           viewport->extents_y.x[0],
	           viewport->extents_x.x[1] - viewport->extents_x.x[0],
	           viewport->extents_y.x[1] - viewport->extents_y.x[0]);
	glEnable (GL_SCISSOR_TEST);

	if (viewport->depth_enabled) {
		/* TODO: other setup */
		glEnable (GL_DEPTH_TEST);
	} else
		glDisable (GL_DEPTH_TEST);

	glClearColor (viewport->clear_color.x[0] / 255.0f,
	              viewport->clear_color.x[1] / 255.0f,
	              viewport->clear_color.x[2] / 255.0f,
	              viewport->clear_color.x[3] / 255.0f);

	/* XXX glClearDepth */
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* DEBUG */
	glColor3f (1.0f, 0.0f, 1.0f);
	glBegin (GL_QUADS);
		glVertex3f (-1.0f, -1.0f, 0.0f);
		glVertex3f ( 1.0f, -1.0f, 0.0f);
		glVertex3f ( 1.0f,  1.0f, 0.0f);
		glVertex3f (-1.0f,  1.0f, 0.0f);
	glEnd ();
#endif
}

void
hikaru_renderer_set_matrix (vk_renderer_t *renderer, mtx4x4f_t *mtx)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->log) {
		VK_LOG ("HR == MATRIX ==");
		VK_LOG ("HR %+9.3f %+9.3f %+9.3f %+9.3f\n"
		        "HR %+9.3f %+9.3f %+9.3f %+9.3f\n"
		        "HR %+9.3f %+9.3f %+9.3f %+9.3f\n"
		        "HR %+9.3f %+9.3f %+9.3f %+9.3f",
		        mtx->x[0][0], mtx->x[0][1], mtx->x[0][2], mtx->x[0][3],
		        mtx->x[1][0], mtx->x[1][1], mtx->x[1][2], mtx->x[1][3],
		        mtx->x[2][0], mtx->x[2][1], mtx->x[2][2], mtx->x[2][3],
		        mtx->x[3][0], mtx->x[3][1], mtx->x[3][2], mtx->x[3][3]);
	}

	/* TODO: set modelview matrix */
}

void
hikaru_renderer_set_material (vk_renderer_t *renderer,
                              hikaru_gpu_material_t *material)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->log) {
		VK_LOG ("HR == MATERIAL ==");
		VK_LOG ("HR color0    : #%02X%02X%02X",
		        material->color[0].x[0],
		        material->color[0].x[1],
		        material->color[0].x[2]);
		VK_LOG ("HR color1    : #%02X%02X%02X",
		        material->color[1].x[0],
		        material->color[1].x[1],
		        material->color[1].x[2]);
		VK_LOG ("HR shininess : #%02X%02X%02X (%02X)",
		        material->shininess.x[0],
		        material->shininess.x[1],
		        material->shininess.x[2],
		        material->specularity);
		VK_LOG ("HR material  : #<%04X,%04X,%04X>",
		        material->material_color.x[0],
		        material->material_color.x[1],
		        material->material_color.x[2]);
		VK_LOG ("HR flags     : mode=%u zblend=%u tex=%u alpha=%u high=%u bmode=%u",
		        material->mode, material->depth_blend,
		        material->has_texture, material->has_alpha,
		        material->has_highlight, material->bmode);
	}

	glColor3f (material->color[1].x[0] / 255.0f,
	           material->color[1].x[1] / 255.0f,
	           material->color[1].x[2] / 255.0f);
}

void
hikaru_renderer_set_texhead (vk_renderer_t *renderer,
                             hikaru_gpu_texhead_t *texhead)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->log) {
		VK_LOG ("HR == TEXHEAD ==");
		VK_LOG ("HR 0C1: byte=%X nibble=%X",
		        texhead->_0C1_byte, texhead->_0C1_nibble);
		VK_LOG ("HR 2C1: %ux%u format=%u unk4=%X unk8=%X",
		        texhead->width, texhead->height, texhead->format,
		        texhead->_2C1_unk4, texhead->_2C1_unk8);
		VK_LOG ("HR 4C1: slot=%X,%X unk=%X",
		        texhead->slotx, texhead->sloty, texhead->_4C1_unk);
	}

	/* TODO: set texture attributes -- bind the proper texture(s) and
	 * set their properties. */
}

void
hikaru_renderer_set_light (vk_renderer_t *renderer,
                           hikaru_gpu_light_t *light)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->log) {
		VK_LOG ("HR == LIGHT ==");
		VK_LOG ("HR ...");
	}

	/* TODO: no idea yet */
}

/* Apparently hikaru triangle strip vertex-linking rules work just like
 * OpenGL's --- that is, 0-1-2, 2-1-3, etc. right-hand rule. */

static void
draw_vertices (hikaru_renderer_t *hr)
{
	if (hr->log) {
		int i;
		VK_LOG ("HR == DRAWING %d VERTICES ==", hr->vertex_index);
		glBegin (GL_TRIANGLE_STRIP);
		for (i = 0; i < hr->vertex_index; i++) {
			hikaru_vertex_t *vtx = &hr->vertices[i];
			VK_LOG ("HR VERTEX %d: %9.3f %9.3f %9.3f | %6.3f %6.3f",
			        i,
			        vtx->pos.x[0], vtx->pos.x[1], vtx->pos.x[2],
			        vtx->texcoords.x[0], vtx->texcoords.x[1]);
			glVertex3f (vtx->pos.x[0], vtx->pos.x[1], vtx->pos.x[2]);
		}
		glEnd ();
	}
}

static void
begin_vertices (hikaru_renderer_t *hr)
{
	if (hr->vertex_index < 0) {
		/* XXX lookup existing VBO or generate a new one. Return
		 * true if a match was found. */
		hr->vertex_index = 0;
		if (hr->log)
			VK_LOG ("HR == BEGIN VBO ==");
	}
}

static void
end_vertices (hikaru_renderer_t *hr)
{
	if (hr->vertex_index >= 0) {
		/* XXX upload the VBO; draw it */
		if (hr->log)
			VK_LOG ("HR == END VBO ==");
		if (hr->vertex_index >= 4)
			draw_vertices (hr);
		hr->vertex_index = -1;
	}
}

void
hikaru_renderer_append_vertex_full (vk_renderer_t *renderer,
                                    vec3f_t *pos,
                                    vec3f_t *normal,
                                    vec2f_t *texcoords)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	if (hr->vertex_index < 0)
		begin_vertices (hr);

	hr->vertices[hr->vertex_index].pos = *pos;
	hr->vertices[hr->vertex_index].normal = *normal;
	hr->vertices[hr->vertex_index].texcoords = *texcoords;

	hr->vertex_index++;
	hr->hack = 0;
}

void
hikaru_renderer_append_vertex (vk_renderer_t *renderer, vec3f_t *pos)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	if (hr->vertex_index < 0)
		begin_vertices (hr);

	hr->vertices[hr->vertex_index].pos = *pos;
	hr->vertices[hr->vertex_index].normal.x[0] = 0.0f;
	hr->vertices[hr->vertex_index].normal.x[1] = 0.0f;
	hr->vertices[hr->vertex_index].normal.x[2] = 0.0f;
	hr->vertices[hr->vertex_index].texcoords.x[0] = 0.0f;
	hr->vertices[hr->vertex_index].texcoords.x[1] = 0.0f;

	hr->hack++;
	VK_ASSERT (hr->hack <= 3);
	if (hr->hack == 3) {
		/* XXX three consecutive Vertex3f calls; start a new model.
		 * Note that this is bogus if Vertex+Normal commands are
		 * followed by Vertex+Texcoord commands... This code will
		 * be rewritten anyway, so meh. */
		
	}

	hr->vertex_index++;
}

void
hikaru_renderer_append_texcoords (vk_renderer_t *renderer,
                                  vec2f_t texcoords[3])
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	VK_ASSERT (hr->vertex_index >= 3);

	/* XXX wrong */
	hr->vertices[hr->vertex_index-3].texcoords = texcoords[0];
	hr->vertices[hr->vertex_index-2].texcoords = texcoords[1];
	hr->vertices[hr->vertex_index-1].texcoords = texcoords[2];

	hr->hack = 0;
}

void
hikaru_renderer_end_vertex_data (vk_renderer_t *renderer)
{
	end_vertices ((hikaru_renderer_t *) renderer);
}

/* 2D Rendering */

void
hikaru_renderer_draw_layer (vk_renderer_t *renderer,
                            vec2i_t coords[2])
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	(void) hr;

	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glOrtho (0.0f,			/* left */
	         renderer->width - 1,	/* right */
	         0.0f,			/* bottom */
	         renderer->height -1,	/* top */
	         0.0f,			/* near */
	         1.0f);			/* far */

	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	glEnable (GL_TEXTURE_2D);

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

/* Texture Upload */

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

void
hikaru_renderer_register_texture (vk_renderer_t *renderer,
                                  hikaru_gpu_texture_t *texture)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* TODO: create a surface, upload the data, add to the texture
	 * table */
}

/* Interface */

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Set an identity projection matrix */
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
#if 0
	glOrtho (0.0f,			/* left */
	         renderer->width - 1,	/* right */
	         -renderer->height + 1,	/* bottom */
	         0.0f,			/* top */
	         0.0f,			/* near */
	         1.0f);			/* far */
#else
	glOrtho (0.0f, 640.0f,
	         -640.0f, 0.0f,
	         -1.0f, 1.0f);
#endif

	/* Set an identity modelview matrix */
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	/* Set the texture matrix */
	glEnable (GL_TEXTURE_2D);
	glMatrixMode (GL_TEXTURE);
	glLoadIdentity ();
	glScalef (1.0f/2048, 1.0f/2048, 1.0f);

	/* Clear the whole framebuffer */
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDisable (GL_TEXTURE_2D);

	/* Upload the TEXRAM data */
	bind_texram (hr);

	/* Reset the temporary vertex data */
	hr->vertex_index = -1;
	hr->hack = 0;
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

		hr->log = vk_util_get_bool_option ("HR_LOG", false);
		if (hr->log) {
			VK_LOG ("HR logging enabled");
		}
	}
	return (vk_renderer_t *) hr;
}
