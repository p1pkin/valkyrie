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

	VK_PRINT ("renderer: GL vendor    = %s", glGetString (GL_VENDOR));
	VK_PRINT ("renderer: GL renderer  = %s", glGetString (GL_RENDERER));
	VK_PRINT ("renderer: GL version   = %s", glGetString (GL_VERSION));
	VK_PRINT ("renderer: GLSL version = %s", glGetString (GL_SHADING_LANGUAGE_VERSION));
	VK_PRINT ("renderer: %d samples on %d ms buffers", num_samples, num_ms_buffers);

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

static GLuint
compile_shader (GLenum type, const char *src)
{
	char info[256];
	GLuint id;
	GLint status;

	id = glCreateShader (type);
	glShaderSource (id, 1, (const GLchar **) &src, NULL);
	glCompileShader (id);
	glGetShaderiv (id, GL_COMPILE_STATUS, &status);
	if (!status) {
		glGetShaderInfoLog (id, sizeof (info), NULL, info);
		VK_ERROR ("could not compile GLSL shader: '%s'\n", info);
		VK_ERROR ("source:\n%s\n", src);
		glDeleteShader (id);
		VK_ASSERT (0);
	}
	return id;
}

GLuint
vk_renderer_compile_program (const char *vs_src, const char *fs_src)
{
	char info[256];
	GLuint id, vs, fs;
	GLint status;

	vs = compile_shader (GL_VERTEX_SHADER, vs_src);
	VK_ASSERT_NO_GL_ERROR ();

	fs = compile_shader (GL_FRAGMENT_SHADER, fs_src);
	VK_ASSERT_NO_GL_ERROR ();

	id = glCreateProgram ();
	glAttachShader (id, vs);
	glAttachShader (id, fs);
	glLinkProgram (id);
	glGetProgramiv (id, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) {
		glGetProgramInfoLog (id, sizeof (info), NULL, info);
		VK_ERROR ("could not link GLSL program: '%s'\n", info);
		VK_ERROR ("vs source:\n%s\n", vs_src);
		VK_ERROR ("fs source:\n%s\n", fs_src);
		glDeleteProgram (id);
		VK_ASSERT (0);
	}

	/* "If a shader object to be deleted is attached to a program object,
	 * it will be flagged for deletion, but it will not be deleted until it
	 * is no longer attached to any program object, for any rendering
	 * context." */
	glDeleteShader (vs);
	glDeleteShader (fs);
	VK_ASSERT_NO_GL_ERROR ();

	return id;
}

void
vk_renderer_destroy_program (GLuint program)
{
	if (program) {
		glUseProgram (0);
		glDeleteProgram (program);
	}
}

void
vk_renderer_print_uniforms (GLuint program)
{
	GLchar id[256];
	GLint count, loc, size, i;
	GLenum type;

	glGetProgramiv (program, GL_ACTIVE_UNIFORMS, &count);
	for (i = 0; i < count; i++) {
		glGetActiveUniform (program, i, sizeof (id), NULL, &size, &type, id);
		loc = glGetUniformLocation (program, id);
		VK_LOG ("uniform %u : %s <size %d>", loc, id, size);
	}
}

/* Taken from Mesa */
void
vk_renderer_ortho (mtx4x4f_t proj, float l, float r, float b, float t, float n, float f)
{
	proj[0][0] = 2.0f / (r - l);
	proj[1][0] = 0.0f;
	proj[2][0] = 0.0f;
	proj[3][0] = - (r + l) / (r - l);

	proj[0][1] = 0.0f;
	proj[1][1] = 2.0f / (t - b);
	proj[2][1] = 0.0f;
	proj[3][1] = - (t + b) / (t - b);

	proj[0][2] = 0.0f;
	proj[1][2] = 0.0f;
	proj[2][2] = -2.0f / (f - n);
	proj[3][2] = - (f + n) / (f - n);

	proj[0][3] = 0.0f;
	proj[1][3] = 0.0f;
	proj[2][3] = 0.0f;
	proj[3][3] = 1.0f;
}

/* Taken from Mesa */
void
vk_renderer_frustum (mtx4x4f_t proj, float l, float r, float b, float t, float n, float f)
{
	proj[0][0] = (2.0f * n) / (r - l);
	proj[1][0] = 0.0f;
	proj[2][0] = (r + l) / (r - l);
	proj[3][0] = 0.0f;

	proj[0][1] = 0.0f;
	proj[1][1] = (2.0f * n) / (t - b);
	proj[2][1] = (t + b) / (t - b);
	proj[3][1] = 0.0f;

	proj[0][2] = 0.0f;
	proj[1][2] = 0.0f;
	proj[2][2] = - (f + n) / (f - n);
	proj[3][2] = - (2.0f * f * n) / (f - n);

	proj[0][3] = 0.0f;
	proj[1][3] = 0.0f;
	proj[2][3] = -1.0f;
	proj[3][3] = 0.0f;
}

/* Taken from Mesa */
bool
vk_renderer_compute_normal_matrix (mtx3x3f_t dst, mtx4x4f_t src)
{
	union {
		mtx4x4f_t m;
		float f[16];
	} inv;
	float det, *s = (float *) src;
	int i, j;

	/* Compute the inverse of the source (projection) matrix. */

	inv.f[0] = s[5]  * s[10] * s[15] - 
	           s[5]  * s[11] * s[14] - 
	           s[9]  * s[6]  * s[15] + 
	           s[9]  * s[7]  * s[14] +
	           s[13] * s[6]  * s[11] - 
	           s[13] * s[7]  * s[10];

	inv.f[4] = -s[4]  * s[10] * s[15] + 
	            s[4]  * s[11] * s[14] + 
	            s[8]  * s[6]  * s[15] - 
	            s[8]  * s[7]  * s[14] - 
	            s[12] * s[6]  * s[11] + 
	            s[12] * s[7]  * s[10];

	inv.f[8] = s[4]  * s[9] * s[15] - 
	           s[4]  * s[11] * s[13] - 
	           s[8]  * s[5] * s[15] + 
	           s[8]  * s[7] * s[13] + 
	           s[12] * s[5] * s[11] - 
	           s[12] * s[7] * s[9];

	inv.f[12] = -s[4]  * s[9] * s[14] + 
	             s[4]  * s[10] * s[13] +
	             s[8]  * s[5] * s[14] - 
	             s[8]  * s[6] * s[13] - 
	             s[12] * s[5] * s[10] + 
	             s[12] * s[6] * s[9];

	inv.f[1] = -s[1]  * s[10] * s[15] + 
	            s[1]  * s[11] * s[14] + 
	            s[9]  * s[2] * s[15] - 
	            s[9]  * s[3] * s[14] - 
	            s[13] * s[2] * s[11] + 
	            s[13] * s[3] * s[10];

	inv.f[5] = s[0]  * s[10] * s[15] - 
	           s[0]  * s[11] * s[14] - 
	           s[8]  * s[2] * s[15] + 
	           s[8]  * s[3] * s[14] + 
	           s[12] * s[2] * s[11] - 
	           s[12] * s[3] * s[10];

	inv.f[9] = -s[0]  * s[9] * s[15] + 
	            s[0]  * s[11] * s[13] + 
	            s[8]  * s[1] * s[15] - 
	            s[8]  * s[3] * s[13] - 
	            s[12] * s[1] * s[11] + 
	            s[12] * s[3] * s[9];

	inv.f[13] = s[0]  * s[9] * s[14] - 
	            s[0]  * s[10] * s[13] - 
	            s[8]  * s[1] * s[14] + 
	            s[8]  * s[2] * s[13] + 
	            s[12] * s[1] * s[10] - 
	            s[12] * s[2] * s[9];

	inv.f[2] = s[1]  * s[6] * s[15] - 
	           s[1]  * s[7] * s[14] - 
	           s[5]  * s[2] * s[15] + 
	           s[5]  * s[3] * s[14] + 
	           s[13] * s[2] * s[7] - 
	           s[13] * s[3] * s[6];

	inv.f[6] = -s[0]  * s[6] * s[15] + 
	            s[0]  * s[7] * s[14] + 
	            s[4]  * s[2] * s[15] - 
	            s[4]  * s[3] * s[14] - 
	            s[12] * s[2] * s[7] + 
	            s[12] * s[3] * s[6];

	inv.f[10] = s[0]  * s[5] * s[15] - 
	            s[0]  * s[7] * s[13] - 
	            s[4]  * s[1] * s[15] + 
	            s[4]  * s[3] * s[13] + 
	            s[12] * s[1] * s[7] - 
	            s[12] * s[3] * s[5];

	inv.f[14] = -s[0]  * s[5] * s[14] + 
	             s[0]  * s[6] * s[13] + 
	             s[4]  * s[1] * s[14] - 
	             s[4]  * s[2] * s[13] - 
	             s[12] * s[1] * s[6] + 
	             s[12] * s[2] * s[5];

	inv.f[3] = -s[1] * s[6] * s[11] + 
	            s[1] * s[7] * s[10] + 
	            s[5] * s[2] * s[11] - 
	            s[5] * s[3] * s[10] - 
	            s[9] * s[2] * s[7] + 
	            s[9] * s[3] * s[6];

	inv.f[7] = s[0] * s[6] * s[11] - 
	           s[0] * s[7] * s[10] - 
	           s[4] * s[2] * s[11] + 
	           s[4] * s[3] * s[10] + 
	           s[8] * s[2] * s[7] - 
	           s[8] * s[3] * s[6];

	inv.f[11] = -s[0] * s[5] * s[11] + 
	             s[0] * s[7] * s[9] + 
	             s[4] * s[1] * s[11] - 
	             s[4] * s[3] * s[9] - 
	             s[8] * s[1] * s[7] + 
	             s[8] * s[3] * s[5];

	inv.f[15] = s[0] * s[5] * s[10] - 
	            s[0] * s[6] * s[9] - 
	            s[4] * s[1] * s[10] + 
	            s[4] * s[2] * s[9] + 
	            s[8] * s[1] * s[6] - 
	            s[8] * s[2] * s[5];

	det = s[0] * inv.f[0] +
	      s[1] * inv.f[4] +
	      s[2] * inv.f[8] +
	      s[3] * inv.f[12];
	if (det == 0.0)
		return false;

	/* Extract the top-left 3x3 matrix from the inverse. */
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			dst[i][j] = inv.m[j][i] * (1.0f / det);

	return true;
}

void
vk_renderer_translate (mtx4x4f_t m, float x, float y, float z)
{
	m[3][0] += m[0][0] * x + m[1][0] * y + m[2][0] * z;
	m[3][1] += m[0][1] * x + m[1][1] * y + m[2][1] * z;
	m[3][2] += m[0][2] * x + m[1][2] * y + m[2][2] * z;
	m[3][3] += m[0][3] * x + m[1][3] * y + m[2][3] * z;
}
