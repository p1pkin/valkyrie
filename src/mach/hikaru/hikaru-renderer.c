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

#include <math.h>

#include "mach/hikaru/hikaru-renderer.h"
#include "vk/surface.h"

#define RESTART_INDEX	0xFFFF

#define MAX_VERTICES_PER_MESH	4096

typedef struct {
	vk_renderer_t base;

	hikaru_gpu_t *gpu;

	struct {
		hikaru_gpu_vertex_t	vbo[MAX_VERTICES_PER_MESH];
		uint16_t		ibo[MAX_VERTICES_PER_MESH];
		unsigned		iindex, vindex;
	} mesh;

	struct {
		vk_surface_t *fb, *texram, *debug;
	} textures;

	struct {
		bool log;
		bool disable_2d;
		bool disable_3d;
		bool draw_fb;
		bool draw_texram;
		bool force_debug_texture;
	} options;

} hikaru_renderer_t;

#define LOG(fmt_, args_...) \
	do { \
		if (hr->options.log) \
			fprintf (stdout, "HR: " fmt_"\n", ##args_); \
	} while (0);

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
	vk_buffer_t *texram = hr->texram[texhead->bank];
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

static vk_surface_t *
decode_texhead_rgba_16 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->texram[texhead->bank];
	uint32_t basex, basey, x, y;
	vk_surface_format_t format;
	vk_surface_t *surface;

	if (texhead->format == HIKARU_FORMAT_RGBA5551)
		format = VK_SURFACE_FORMAT_RGBA5551;
	else
		format = VK_SURFACE_FORMAT_RGBA4444;

	surface = vk_surface_new (texhead->width, texhead->height, format);
	if (!surface)
		return NULL;

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty); 

	for (y = 0; y < texhead->height; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < texhead->width; x++) {
			uint32_t offs  = base + x * 2;
			uint16_t texel = vk_buffer_get (texram, 2, offs);
			vk_surface_put16 (surface, x, y, texel);
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

/****************************************************************************
 3D Rendering
****************************************************************************/

static void
upload_current_state (hikaru_renderer_t *hr)
{
	/* Viewport */
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glOrtho ((GLfloat) vp->extents_x[0],	/* left */
	         (GLfloat) vp->extents_x[1],	/* right */
	         -(GLfloat) vp->extents_y[1],	/* bottom */
	         (GLfloat) vp->extents_y[0],	/* top */
	         -1.0f, 1.0f);			/* near, far */

	/* Modelview */
	glMatrixMode (GL_MODELVIEW);
	glLoadMatrixf ((GLfloat *) &hr->current.modelview.mtx[0][0]);

	/* Material */
	if (mat->has_texture)
		glEnable (GL_TEXTURE_2D);
	else
		glDisable (GL_TEXTURE_2D);

	/* Texhead */
	hikaru_gpu_texhead_t *tex = &hr->current.texhead;
	if (tex && !tex->uploaded) {
		vk_surface_t *surface = NULL;

		tex->uploaded = 1;

		/* Delete the old texture */
		if (hr->current.texture != hr->textures.debug)
			vk_surface_destroy (&hr->current.texture);

		/* XXX implement texhead caching */

		/* Decode the texhead data into a vk_surface */
		switch (tex->format) {
		case HIKARU_FORMAT_RGBA5551:
		case HIKARU_FORMAT_RGBA4444:
			surface = decode_texhead_rgba_16 (hr, tex);
			break;
		case HIKARU_FORMAT_RGBA1111:
			surface = decode_texhead_rgba1111 (hr, tex);
			break;
		case HIKARU_FORMAT_ALPHA8:
			surface = decode_texhead_a8 (hr, tex);
			break;
		default:
			VK_ASSERT (0);
			break;
		}

		/* If no surface can be generated for the given texhead, bind
		 * the debug texture. */
		if (!surface || hr->options.force_debug_texture)
			vk_surface_bind (hr->textures.debug);
		else {
			/* Upload the decoded texhead data */
			hr->current.texture = surface;
			vk_surface_commit (surface);
		}
	}

	/* Lighting */
	glDisable (GL_LIGHTING);
}

void
hikaru_renderer_begin_mesh (vk_renderer_t *renderer, bool is_static)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->options.disable_3d)
		return;

	
}

void
hikaru_renderer_end_mesh (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->options.disable_3d)
		return;
}

/****************************************************************************
 2D Rendering
****************************************************************************/

static vk_surface_t *
decode_layer_rgba5551 (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;
	uint32_t x, y;

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA5551);
	if (!surface)
		return NULL;

	for (y = 0; y < 480; y++) {
		uint32_t yoffs = (layer->y0 + y) * 4096;
		for (x = 0; x < 640; x++) {
			uint32_t offs = yoffs + layer->x0 * 4 + x * 2;
			uint32_t texel = vk_buffer_get (hr->fb, 2, offs);
			vk_surface_put16 (surface, x ^ 1, y, texel);
		}
	}
	return surface;
}

static vk_surface_t *
decode_layer_rgba8888 (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;
	uint32_t x, y;

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA8888);
	if (!surface)
		return NULL;

	for (y = 0; y < 480; y++) {
		for (x = 0; x < 640; x++) {
			uint32_t offs = coords_to_offs_32 (layer->x0 + x, layer->y0 + y);
			uint32_t texel = vk_buffer_get (hr->fb, 4, offs);
			vk_surface_put32 (surface, x, y, texel);
		}
	}
	return surface;
}

static vk_surface_t *
upload_layer (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;

	if (layer->format == HIKARU_FORMAT_RGBA8888)
		surface = decode_layer_rgba8888 (hr, layer);
	else
		surface = decode_layer_rgba5551 (hr, layer);

	vk_surface_commit (surface);
	return surface;
}

void
hikaru_renderer_draw_layer (vk_renderer_t *renderer, hikaru_gpu_layer_t *layer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	vk_surface_t *surface;

	if (hr->options.disable_2d)
		return;

	LOG ("drawing layer %s", get_gpu_layer_str (layer));

	glDisable (GL_SCISSOR_TEST);

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
 Debug
****************************************************************************/

static void
upload_fb (hikaru_renderer_t *hr)
{
	uint32_t x, y;
	VK_ASSERT (hr->textures.fb);
	for (y = 0; y < 2048; y++) {
		uint32_t yoffs = y * 4096;
		for (x = 0; x < 2048; x++) {
			uint32_t offs = yoffs + x * 2;
			uint32_t texel = vk_buffer_get (hr->fb, 2, offs);
			vk_surface_put16 (hr->textures.fb, x ^ 1, y, texel);
		}
	}
	vk_surface_commit (hr->textures.fb);
}

static void
upload_texram (hikaru_renderer_t *hr)
{
	uint32_t x, y;
	VK_ASSERT (hr->textures.texram);
	for (y = 0; y < 1024; y++) {
		uint32_t yoffs = y * 4096;
		for (x = 0; x < 2048; x++) {
			uint32_t texel, offs = yoffs + x * 2;
			texel = vk_buffer_get (hr->texram[0], 2, offs);
			vk_surface_put16 (hr->textures.texram, x ^ 1, y, texel);
			texel = vk_buffer_get (hr->texram[1], 2, offs);
			vk_surface_put16 (hr->textures.texram, 1024 + (x ^ 1), y, texel);
		}
	}
	vk_surface_commit (hr->textures.texram);
}

/****************************************************************************
 Interface
****************************************************************************/

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Erase the current rendering state*/
	memset (&hr->current, 0, sizeof (hr->current));

	/* Delete all meshes (XXX actually cache them) */
	free_meshes (hr);

	/* clear the frame buffer to a bright pink color */
	glClearColor (1.0f, 0.0f, 1.0f, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Draw the FB and/or TEXRAM if asked to do so */
	if (hr->options.draw_fb) {
		upload_fb (hr);
		vk_surface_draw (hr->textures.fb);
	}
	if (hr->options.draw_texram) {
		upload_texram (hr);
		vk_surface_draw (hr->textures.texram);
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

	free_meshes (hr);
	free_textures (hr);
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;
		vk_surface_destroy (&hr->current.texture);
		vk_surface_destroy (&hr->textures.fb);
		vk_surface_destroy (&hr->textures.texram);
		vk_surface_destroy (&hr->textures.debug);
		free_meshes (hr);
		free_textures (hr);
	}
}

static vk_surface_t *
build_debug_surface (void)
{
	/* Build a colorful 2x2 checkerboard surface */
	vk_surface_t *surface = vk_surface_new (2, 2, VK_SURFACE_FORMAT_RGBA4444);
	if (!surface)
		return NULL;
	vk_surface_put16 (surface, 0, 0, 0xF00F);
	vk_surface_put16 (surface, 0, 1, 0xF0F0);
	vk_surface_put16 (surface, 1, 0, 0xFF00);
	vk_surface_put16 (surface, 1, 1, 0xFFFF);
	vk_surface_commit (surface);
	return surface;
}

static bool
build_default_surfaces (hikaru_renderer_t *hr)
{
	if (hr->options.draw_fb) {
		hr->textures.fb = vk_surface_new (
			2048, 2048, VK_SURFACE_FORMAT_RGBA5551);
		if (!hr->textures.fb) {
			VK_ERROR ("HR: failed to create FB surface, turning off");
			hr->options.draw_fb = false;
		}
	}

	if (hr->options.draw_texram) {
		hr->textures.texram = vk_surface_new (
			2048, 2048, VK_SURFACE_FORMAT_RGBA5551);
		if (!hr->textures.texram) {
			VK_ERROR ("HR: failed to create TEXRAM surface, turning off");
			hr->options.draw_texram = false;
		}
	}

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

	/* Setup machine buffers */
	hr->fb = fb;
	hr->texram[0] = texram[0];
	hr->texram[1] = texram[1];

	/* Read options from the environment */
	hr->options.log =
		vk_util_get_bool_option ("HR_LOG", false);
	hr->options.disable_2d =
		vk_util_get_bool_option ("HR_DISABLE_2D", false);
	hr->options.disable_3d =
		vk_util_get_bool_option ("HR_DISABLE_3D", false);
	hr->options.draw_fb =
		vk_util_get_bool_option ("HR_DRAW_FB", false);
	hr->options.draw_texram =
		vk_util_get_bool_option ("HR_DRAW_TEXRAM", false);
	hr->options.force_debug_texture =
		vk_util_get_bool_option ("HR_FORCE_DEBUG_TEXTURE", false);

	/* Create a few surfaces */
	if (!build_default_surfaces (hr))
		goto fail;

	return (vk_renderer_t *) hr;

fail:
	hikaru_renderer_destroy ((vk_renderer_t **) &hr);
	return NULL;
}
