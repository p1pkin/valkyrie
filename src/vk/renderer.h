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

#ifndef __VK_REND_H__
#define __VK_REND_H__

#include "vk/core.h"

#define VK_ASSERT_NO_GL_ERROR() \
	do { \
		GLenum error = glGetError (); \
		char *msg = NULL; \
		switch (error) { \
		case GL_INVALID_ENUM: \
			msg = "invalid enum"; \
			break; \
		case GL_INVALID_VALUE: \
			msg = "invalid value"; \
			break; \
		case GL_INVALID_OPERATION: \
			msg = "invalid operation"; \
			break; \
		case GL_INVALID_FRAMEBUFFER_OPERATION: \
			msg = "invalid fb operation"; \
			break; \
		case GL_OUT_OF_MEMORY: \
			msg = "out of memory"; \
			break; \
		default: \
			break; \
		} \
		if (msg) \
			VK_ERROR ("GL ERROR: %s", msg); \
		VK_ASSERT (error == GL_NO_ERROR); \
	} while (0)

typedef struct vk_renderer_t vk_renderer_t;

struct vk_renderer_t {
	SDL_Window *window;
	SDL_GLContext *gl_context;

	unsigned width;
	unsigned height;
	char message[256];

	void	(* destroy)(vk_renderer_t **renderer_);
	void	(* reset)(vk_renderer_t *renderer);
	void	(* begin_frame)(vk_renderer_t *renderer);
	void	(* end_frame)(vk_renderer_t *renderer);
};

vk_renderer_t		*vk_renderer_new (unsigned width, unsigned height);
int			 vk_renderer_init (vk_renderer_t *renderer);
void			 vk_renderer_begin_frame (vk_renderer_t *renderer);
void			 vk_renderer_end_frame (vk_renderer_t *renderer);
void			 vk_renderer_clear_gl_errors (void);

static inline void
vk_renderer_reset (vk_renderer_t *renderer)
{
	renderer->reset (renderer);
}

static inline void
vk_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_)
		(*renderer_)->destroy (renderer_);
}

#endif /* __VK_REND_H__ */

