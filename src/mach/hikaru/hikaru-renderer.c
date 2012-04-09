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

/* XXX fix the texcoords to work with GL's tri-strips */
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
	struct {
		bool enable_logging;
		bool enable_2d;
		bool enable_3d;
	} options;

	/* Texture data */
	vk_buffer_t *texram;
	vk_surface_t *texture;
	hikaru_texture_t textures[256][256];

	/* Modelview Matrix */
	unsigned modelview_num_up;
	mtx4x4f_t modelview_matrix;

	/* Model data */
	hikaru_vertex_t *models[64];
	hikaru_vertex_t	vertices[1024];
	int vertex_index;
	int model_index;
	int hack;

} hikaru_renderer_t;

/* 3D Rendering */

void
hikaru_renderer_set_viewport (vk_renderer_t *renderer,
                              hikaru_gpu_viewport_t *viewport)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (!hr->options.enable_3d)
		return;

	if (hr->options.enable_logging)
		VK_LOG ("HR: Set VIEWPORT %s",
		        get_gpu_viewport_str (viewport));

	/* Hikaru's origin is at the top left; x axis extends to the
	 * right; y axis extends downwards. How come? */
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glOrtho ((GLfloat) viewport->extents_x.x[0],	/* left */
	         (GLfloat) viewport->extents_x.x[1],	/* right */
	         -(GLfloat) viewport->extents_y.x[1],	/* bottom */
	         (GLfloat) viewport->extents_y.x[0],	/* top */
	         -1.0f, 1.0f);

	VK_LOG ("HR viewport: %f %f %f %f",
		(GLfloat) viewport->extents_x.x[0],
		(GLfloat) viewport->extents_x.x[1],
		-(GLfloat) viewport->extents_y.x[1],
		(GLfloat) viewport->extents_y.x[0]);

	glScissor (viewport->extents_x.x[0], /* lower left */
	           viewport->extents_y.x[0],
	           viewport->extents_x.x[1] - viewport->extents_x.x[0],
	           viewport->extents_y.x[1] - viewport->extents_y.x[0]);
	glEnable (GL_SCISSOR_TEST);

	glClearColor (viewport->clear_color.x[0] / 255.0f,
	              viewport->clear_color.x[1] / 255.0f,
	              viewport->clear_color.x[2] / 255.0f,
	              viewport->clear_color.x[3] / 255.0f);

	/* XXX clear depth too? */
	glClear (GL_COLOR_BUFFER_BIT);
}

void
hikaru_renderer_set_material (vk_renderer_t *renderer,
                              hikaru_gpu_material_t *material)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (!hr->options.enable_3d)
		return;

	if (hr->options.enable_logging)
		VK_LOG ("HR: Set MATERIAL %s",
		        get_gpu_material_str (material));

	glColor3f (material->color[1].x[0] / 255.0f,
	           material->color[1].x[1] / 255.0f,
	           material->color[1].x[2] / 255.0f);

	if (material->has_texture)
		glEnable (GL_TEXTURE_2D);
	else
		glDisable (GL_TEXTURE_2D);
}

void
hikaru_renderer_set_texhead (vk_renderer_t *renderer,
                             hikaru_gpu_texhead_t *texhead)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (!hr->options.enable_3d)
		return;

	if (hr->options.enable_logging)
		VK_LOG ("HR: Set TEXHEAD %s",
		        get_gpu_texhead_str (texhead));

	/* For now just bind the default surface and work in 2048x2048
	 * space. */
	vk_surface_bind (hr->texture);
}

void
hikaru_renderer_set_light (vk_renderer_t *renderer,
                           hikaru_gpu_light_t *light)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (!hr->options.enable_3d)
		return;

	if (hr->options.enable_logging)
		VK_LOG ("HR: Set LIGHT");
}

void
hikaru_renderer_set_modelview_vertex (vk_renderer_t *renderer,
                                      unsigned n, unsigned m,
                                      vec3f_t *vertex)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (!hr->options.enable_3d)
		return;

	/* It's a row vertex */

	hr->modelview_matrix.x[0][n] = vertex->x[0];
	hr->modelview_matrix.x[1][n] = vertex->x[1];
	hr->modelview_matrix.x[2][n] = vertex->x[2];
	hr->modelview_matrix.x[3][n] = (n == 3) ? 1.0f : 0.0f;

	hr->modelview_num_up++;
	if (hr->modelview_num_up == 4) {
		hr->modelview_num_up = 0;
		if (hr->options.enable_logging) {
			VK_LOG ("HR == MODELVIEW MATRIX ==");
			VK_LOG ("HR [ %9.3f %9.3f %9.3f %9.3f ]",
			        hr->modelview_matrix.x[0][0],
			        hr->modelview_matrix.x[1][0],
			        hr->modelview_matrix.x[2][0],
			        hr->modelview_matrix.x[3][0]);
			VK_LOG ("HR [ %9.3f %9.3f %9.3f %9.3f ]",
			        hr->modelview_matrix.x[0][1],
			        hr->modelview_matrix.x[1][1],
			        hr->modelview_matrix.x[2][1],
			        hr->modelview_matrix.x[3][1]);
			VK_LOG ("HR [ %9.3f %9.3f %9.3f %9.3f ]",
			        hr->modelview_matrix.x[0][2],
			        hr->modelview_matrix.x[1][2],
			        hr->modelview_matrix.x[2][2],
			        hr->modelview_matrix.x[3][2]);
			VK_LOG ("HR [ %9.3f %9.3f %9.3f %9.3f ]",
			        hr->modelview_matrix.x[0][3],
			        hr->modelview_matrix.x[1][3],
			        hr->modelview_matrix.x[2][3],
			        hr->modelview_matrix.x[3][3]);
		}

		/* Load the modelview matrix */
		glMatrixMode (GL_MODELVIEW);
		glLoadMatrixf ((GLfloat *) hr->modelview_matrix.x);
	}
}

void
hikaru_renderer_register_texture (vk_renderer_t *renderer,
                                  hikaru_gpu_texture_t *texture)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (!hr->options.enable_3d)
		return;

	/* TODO: create a surface, upload the data, add to the texture
	 * table */
}

/* Apparently hikaru triangle strip vertex-linking rules work just like
 * OpenGL's --- that is, 0-1-2, 2-1-3, etc. right-hand rule. Tex coords
 * are a bit tougher... */

static void
draw_vertices (hikaru_renderer_t *hr)
{
	int i;
	if (hr->options.enable_logging)
		VK_LOG ("HR == DRAWING %d VERTICES ==", hr->vertex_index);

	glDisable (GL_TEXTURE_2D);

	glBegin (GL_TRIANGLE_STRIP);
	for (i = 0; i < hr->vertex_index; i++) {
		hikaru_vertex_t *vtx = &hr->vertices[i];
		glTexCoord2fv (vtx->texcoords.x);
		glVertex3fv (vtx->pos.x);

		if (hr->options.enable_logging)
			VK_LOG ("HR VERTEX %d: %9.3f %9.3f %9.3f | %6.3f %6.3f",
			        i,
			        vtx->pos.x[0], vtx->pos.x[1], vtx->pos.x[2],
			        vtx->texcoords.x[0], vtx->texcoords.x[1]);
	}
	glEnd ();
}

static void
begin_vertices (hikaru_renderer_t *hr)
{
	if (hr->vertex_index < 0) {
		/* XXX lookup existing VBO or generate a new one. Return
		 * true if a match was found. */
		hr->vertex_index = 0;
		if (hr->options.enable_logging)
			VK_LOG ("HR == BEGIN VBO ==");
	}
}

static void
end_vertices (hikaru_renderer_t *hr)
{
	if (hr->vertex_index >= 0) {
		/* XXX upload the VBO; draw it */
		if (hr->options.enable_logging)
			VK_LOG ("HR == END VBO ==");
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
	if (!hr->options.enable_3d)
		return;

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
	if (!hr->options.enable_3d)
		return;

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
		hr->hack = 0;
	}

	hr->vertex_index++;
}

void
hikaru_renderer_append_texcoords (vk_renderer_t *renderer,
                                  vec2f_t texcoords[3])
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	if (!hr->options.enable_3d)
		return;

	if (hr->vertex_index < 3) {
		VK_ERROR ("HR: bad texcoords call, vertex_index=%d, skipping", hr->vertex_index);
	} else if (hr->vertex_index == 3) {
		/* If it's the first */
		int i, j = hr->vertex_index - 1;
		for (i = 0; i < 3; i++, j--) {
			hr->vertices[j].texcoords = texcoords[i];
		}
	} else {
		/* TODO append only to the last vertex and check that the
		 * texcoords match the others. */
	}

	hr->hack = 0;
}

#if 0
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
#endif

void
hikaru_renderer_end_vertex_data (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	if (!hr->options.enable_3d)
		return;
	end_vertices (hr);
}

/* 2D Rendering */

void
hikaru_renderer_draw_layer (vk_renderer_t *renderer,
                            uint32_t x0, uint32_t y0,
                            uint32_t x1, uint32_t y1)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (!hr->options.enable_2d)
		return;

	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glOrtho (0.0f, 640.0f,	/* left, right */
	         0.0f, 480.0f,	/* bottom, top */
	         -1.0f, 1.0f);	/* near, far */

	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	glMatrixMode (GL_TEXTURE);
	glLoadIdentity ();
	glScalef (1.0f/2048, 1.0f/2048, 1.0f);

	glEnable (GL_TEXTURE_2D);
	vk_surface_bind (hr->texture);

	glBegin (GL_TRIANGLE_STRIP);
		glTexCoord2s (x0, y0); glVertex3f (0.0f, 0.0f, 0.1f);
		glTexCoord2s (x1, y0); glVertex3f (639.0f, 0.0f, 0.1f);
		glTexCoord2s (x0, y1); glVertex3f (0.0f, 480.0f, 0.1f);
		glTexCoord2s (x1, y1); glVertex3f (639.0f, 480.0f, 0.1f);
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
upload_texram (hikaru_renderer_t *hr)
{
	unsigned x, y;

	/* XXX this is _very_ slow; it's one of the main CPU hogs in
	 * valkyrie. The solution is to stop doing it all the time, and
	 * probably just upload the layer areas. */
	for (y = 0; y < 2048; y++) {
		uint32_t yoffs = y * 4096;
		for (x = 0; x < 2048; x++) {
			uint32_t offs = yoffs + x * 2;
			uint32_t texel = rgba4_to_rgba8 (vk_buffer_get (hr->texram, 2, offs));
			/* Note: the xor here is needed; is the GPU a big
			 * endian device? */
			vk_surface_put32 (hr->texture, x ^ 1, y, texel);
		}
	}

	vk_surface_commit (hr->texture);
}

/* Interface */

/* The order in which the following are called is: begin_frame, vblank_in,
 * vblank_out, end_frame. */

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Setup some state */
	hr->modelview_num_up = 0;
	hr->vertex_index = -1;
	hr->hack = 0;

	/* Setup default GL state; this is useful if any of the state
	 * update instructions is not called during in the GPU command
	 * stream. */
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glOrtho (0.0f,		/* left */
	         640.0f,	/* right */
	         -640.0f,	/* bottom */
	         0.0f,		/* top */
	         -1.0f,		/* near */
	         1.0f);		/* far */

	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	glClearColor (1.0f, 0.0f, 1.0f, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	upload_texram (hr);
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

		/* Read options from the environment */
		hr->options.enable_logging =
			vk_util_get_bool_option ("HR_LOG", false);
		hr->options.enable_2d =
			vk_util_get_bool_option ("HR_ENABLE_2D", true);
		hr->options.enable_3d =
			vk_util_get_bool_option ("HR_ENABLE_3D", true);

		if (hr->options.enable_logging) {
			VK_LOG ("HR: logging enabled; 2d=%d 3d=%d",
			        hr->options.enable_2d, hr->options.enable_3d);
		}
	}
	return (vk_renderer_t *) hr;
}
