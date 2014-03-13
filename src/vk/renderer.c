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

static uint32_t clock;

void
vk_renderer_end_frame (vk_renderer_t *renderer)
{
	char title[256];
	uint32_t temp, delta;
	float fps;

	if (renderer->end_frame)
		renderer->end_frame (renderer);
	SDL_GL_SwapBuffers ();

	temp = SDL_GetTicks ();
	delta = temp - clock;
	clock = temp;

	fps = 0.0f;
	if (delta)
		fps = 1000.0f / delta;

	sprintf (title, "Valkyrie (%4.1f FPS) [%s]", fps, renderer->message);
	SDL_WM_SetCaption (title, "Valkyrie");
}

int
vk_renderer_init (vk_renderer_t *renderer)
{
	const SDL_VideoInfo *info;

	if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
		VK_ERROR ("could not initialize SDL: '%s'", SDL_GetError ());
		return -1;
	}

	info = SDL_GetVideoInfo ();
	if (!info) {
		VK_ERROR ("could not query SDL video info: '%s'", SDL_GetError ());
		return -1;
	}

	/* We set the framebuffer always to RGBA32 (the actual component
	 * order depends on what OpenGL provides. */

	SDL_GL_SetAttribute (SDL_GL_RED_SIZE,   8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE,  8);
	SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);

	if (!SDL_SetVideoMode (renderer->width, renderer->height, 32, SDL_OPENGL)) {
		VK_ERROR ("could not set video mode: '%s'", SDL_GetError ());
		return -1;
	}

	SDL_WM_SetCaption ("Valkyrie", "Valkyrie");

	clock = SDL_GetTicks ();

	if (glewInit () != GLEW_OK) {
		VK_ERROR ("could not initialize glew");
		return -1;
	}

	/* Set the GL viewport (and scissor) size */
	glViewport (0, 0, renderer->width, renderer->height);

	/* Set an orthographic projection matrix */
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();

	/* Set an identity modelview matrix */
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

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
