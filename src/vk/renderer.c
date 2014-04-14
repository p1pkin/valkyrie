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

#include "vk/renderer.h"

/* TODO change API to avoid leaking the SDL window and GL context. */

void
vk_renderer_clear_gl_errors (void)
{
	while (glGetError () != GL_NO_ERROR);
}

void
vk_renderer_begin_frame (vk_renderer_t *renderer)
{
	renderer->message[0] = '\0';
	if (renderer->begin_frame)
		renderer->begin_frame (renderer);
}

static uint32_t clock = 0;

void
vk_renderer_end_frame (vk_renderer_t *renderer)
{
	char title[256];
	uint32_t temp, delta;
	float fps;

	VK_ASSERT (renderer);

	if (renderer->end_frame)
		renderer->end_frame (renderer);
	SDL_GL_SwapWindow (renderer->window);

	temp = SDL_GetTicks ();
	delta = temp - clock;
	clock = temp;

	fps = 0.0f;
	if (delta)
		fps = 1000.0f / delta;

	sprintf (title, "Valkyrie (%4.1f FPS) [%s]", fps, renderer->message);
	SDL_SetWindowTitle (renderer->window, title);
}

int
vk_renderer_init (vk_renderer_t *renderer)
{
	int num_ms_buffers, num_samples;

	VK_ASSERT (renderer);
	VK_ASSERT (renderer->width);
	VK_ASSERT (renderer->height);

	if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
		VK_ERROR ("could not initialize SDL: '%s'", SDL_GetError ());
		return -1;
	}

	SDL_GL_SetAttribute (SDL_GL_RED_SIZE,   8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE,  8);
	SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BUFFER_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_RED_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_GREEN_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_BLUE_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCUM_ALPHA_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_STEREO, 0);
	SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, 0);
	SDL_GL_SetAttribute (SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS,
	                     SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK,
	                     SDL_GL_CONTEXT_PROFILE_CORE);

	renderer->window = SDL_CreateWindow ("Valkyrie",
	                                     SDL_WINDOWPOS_CENTERED,
	                                     SDL_WINDOWPOS_CENTERED,
	                                     renderer->width, renderer->height,
	                                     SDL_WINDOW_OPENGL);
	if (!renderer->window) {
		VK_ERROR ("could not create SDL window: '%s'", SDL_GetError ());
		return -1;
	}

	renderer->gl_context = SDL_GL_CreateContext (renderer->window);
	if (!renderer->gl_context) {
		VK_ERROR ("could not create GL context: '%s'", SDL_GetError ());
		return -1;
	}

	SDL_GL_SetSwapInterval (0);

	glewExperimental = GL_TRUE;
	if (glewInit () != GLEW_OK) {
		VK_ERROR ("could not initialize glew");
		return -1;
	}

	/* Attempt to force multisampling off. */
	glDisable (GL_MULTISAMPLE);

	SDL_GL_GetAttribute (SDL_GL_MULTISAMPLEBUFFERS, &num_ms_buffers);
	SDL_GL_GetAttribute (SDL_GL_MULTISAMPLESAMPLES, &num_samples);

	VK_LOG ("renderer: GL vendor    = %s", glGetString (GL_VENDOR));
	VK_LOG ("renderer: GL renderer  = %s", glGetString (GL_RENDERER));
	VK_LOG ("renderer: GL version   = %s", glGetString (GL_VERSION));
	VK_LOG ("renderer: GLSL version = %s", glGetString (GL_SHADING_LANGUAGE_VERSION));
	VK_LOG ("renderer: %d samples on %d ms buffers", num_samples, num_ms_buffers);

	glViewport (0, 0, renderer->width, renderer->height);

	/* The SDL initialization sequence does not clear the (harmless,
	 * hopefully) GL errors it may cause. Let's do it here so we can
	 * catch more serious errors later. */
	vk_renderer_clear_gl_errors ();

	return 0;
}

vk_renderer_t *
vk_renderer_new (unsigned width, unsigned height)
{
	vk_renderer_t *renderer = ALLOC (vk_renderer_t);
	if (!renderer)
		return NULL;

	renderer->width = width;
	renderer->height = height;

	renderer->begin_frame = NULL;
	renderer->end_frame = NULL;
	renderer->destroy = NULL;

	vk_renderer_init (renderer);

	return renderer;
}
