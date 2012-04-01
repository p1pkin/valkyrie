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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "vk/core.h"
#include "vk/renderer.h"

/* Add a generic on-screen FPS counter */

static const char *default_extensions[] = {
	"GL_ARB_fragment_shader",
	"GL_ARB_vertex_shader",
	"GL_ARB_shader_objects",
	""
};

/* Misc */

void
vk_renderer_clear_gl_errors (void)
{
	while (glGetError () != GL_NO_ERROR);
}

bool
vk_renderer_are_extensions_supported (const char *extensions[])
{
	unsigned i;

	for (i = 0; strlen (extensions[i]) > 0; i++)
		if (glewGetExtension (extensions[i]) != GL_TRUE)
			return false;
	return true;
}

/* vk_shader_t */

static int
vk_shader_compile (vk_shader_t *shader)
{
	GLchar error[256];
	GLint res, len;

	VK_ASSERT (shader);

	glShaderSource (shader->id, 1, (const GLchar **) &shader->source, NULL);
	glCompileShader (shader->id);
	glGetShaderiv (shader->id, GL_COMPILE_STATUS, &res);

	if (!res) {
		glGetShaderInfoLog (shader->id, 256, &len, error);
		VK_ERROR ("could not compile shader :'%s'\n"
		          "source:\n"
		          "%s", error, shader->source);
		return -1;
	}
	return 0;
}

void
vk_shader_delete (vk_shader_t **shader_)
{
	if (shader_) {
		vk_shader_t *shader = *shader_;
		if (shader) {
			if (shader->id)
				glDeleteShader (shader->id);
			shader->id = 0;
			free (shader->source);
			shader->source = NULL;
		}
		*shader_ = NULL;
	}
}

vk_shader_t *
vk_shader_new_from_file (const char *path, GLenum type)
{
	vk_shader_t *shader = ALLOC (vk_shader_t);
	if (!shader)
		goto fail;

	shader->id = glCreateShader (type);
	if (!shader->id)
		goto fail;	

	shader->source = vk_load_any (path, NULL);
	if (!shader->source)
		goto fail;

	if (vk_shader_compile (shader))
		goto fail;

	return shader;

fail:
	vk_shader_delete (&shader);
	return NULL;
}

/* vk_program */

vk_shader_program_t *
vk_shader_program_new_from_files (const char *vs_path,
                                  const char *fs_path)
{
	vk_shader_program_t *program;
	GLchar error[256];
	GLint result, len;

	program = ALLOC (vk_shader_program_t);
	if (!program)
		goto fail;

	program->id = glCreateProgram ();
	if (!program->id)
		goto fail;

	program->vs = vk_shader_new_from_file (vs_path, GL_VERTEX_SHADER);
	program->fs = vk_shader_new_from_file (fs_path, GL_FRAGMENT_SHADER);

	if (!program->vs || !program->fs)
		goto fail;

	glAttachShader (program->id, program->vs->id);
	glAttachShader (program->id, program->fs->id);

	glLinkProgram (program->id);
	glGetProgramiv (program->id, GL_LINK_STATUS, &result);
	if (result != GL_TRUE) {
		glGetProgramInfoLog (program->id, 256, &len, error);
		VK_ERROR ("could not links shaders: %s", error);
		goto fail;
	}

	return program;

fail:
	vk_shader_program_delete (&program);
	return NULL;
}

vk_shader_program_t *
vk_shader_program_new_passthru (void)
{
	return vk_shader_program_new_from_files ("shaders/passthru.vs", "shaders/passthru.fs");
}

void
vk_renderer_set_shader_program (vk_renderer_t *renderer, vk_shader_program_t *program)
{
	renderer->program = program;
	glUseProgram (!program ? 0 : program->id);
}

void
vk_shader_program_delete (vk_shader_program_t **program_)
{
	if (program_) {
		vk_shader_program_t *program = *program_;
		GLint id;

		/* Make sure we don't delete the current program */
		glGetIntegerv (GL_CURRENT_PROGRAM, &id);
		VK_ASSERT (program->id != id);

		vk_shader_delete (&program->vs);
		vk_shader_delete (&program->fs);

		glDeleteProgram (program->id);
		free (program);

		*program_ = NULL;
	}
}

void
vk_renderer_begin_frame (vk_renderer_t *renderer)
{
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (renderer->begin_frame)
		renderer->begin_frame (renderer);
}

void
vk_renderer_end_frame (vk_renderer_t *renderer)
{
	if (renderer->end_frame)
		renderer->end_frame (renderer);
	SDL_GL_SwapBuffers ();
}

int
vk_renderer_init (vk_renderer_t *renderer)
{
	const SDL_VideoInfo *info;

	if (SDL_Init (SDL_INIT_VIDEO)) {
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

	if (glewInit () != GLEW_OK) {
		VK_ERROR ("could not initialize glew");
		return -1;
	}

	if (!vk_renderer_are_extensions_supported (default_extensions)) {
		VK_ERROR ("required extensions missing");
		return -1;
	}

	/* Set the GL viewport (and scissor) size */
	glViewport (0, 0, renderer->width, renderer->height);

	/* Set an orthographic projection matrix */
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	gluOrtho2D (0.0f, renderer->width - 1, /* left, right */
	            0.0f, renderer->height - 1); /* bottom, top */

	/* Set an identity modelview matrix */
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	/* Set the clear color */
	glClearColor (0.0, 0.0, 0.0, 1.0);

	/* Z-Test */
	glDisable (GL_DEPTH_TEST);

	/* Blending */
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* Shading Model */
	glShadeModel (GL_SMOOTH);

	/* Culling */
	glDisable (GL_CULL_FACE);

	/* Lighting */
	glDisable (GL_LIGHTING);

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

	renderer->begin_frame = vk_renderer_begin_frame;
	renderer->end_frame = NULL;
	renderer->delete = NULL;

	vk_renderer_init (renderer);

	return renderer;
}
