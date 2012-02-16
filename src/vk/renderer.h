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

#include <stdint.h>
#include <stdbool.h>

#include <GL/glew.h>
#include <SDL/SDL.h>

typedef struct {
	GLuint id;
	GLchar *source;
} vk_shader_t;

typedef struct {
	GLuint id;
	vk_shader_t *vs;
	vk_shader_t *fs;
} vk_shader_program_t;

typedef struct vk_renderer_t vk_renderer_t;

struct vk_renderer_t {
	unsigned width;
	unsigned height;
	vk_shader_program_t *program;

	void	(* delete)(vk_renderer_t **renderer_);
	void	(* reset)(vk_renderer_t *renderer);
	void	(* begin_frame)(vk_renderer_t *renderer);
	void	(* end_frame)(vk_renderer_t *renderer);
};

vk_shader_t		*vk_shader_new_from_file (const char *path, GLenum type);
void			 vk_shader_delete (vk_shader_t **shader_);

vk_shader_program_t	*vk_shader_program_new_from_files (const char *vs_path, const char *fs_path);
vk_shader_program_t	*vk_shader_program_new_passthru (void);
void			 vk_shader_program_delete (vk_shader_program_t **program_);

vk_renderer_t		*vk_renderer_new (unsigned width, unsigned height);
int			 vk_renderer_init (vk_renderer_t *renderer);
void			 vk_renderer_begin_frame (vk_renderer_t *renderer);
void			 vk_renderer_end_frame (vk_renderer_t *renderer);
void			 vk_renderer_set_shader_program (vk_renderer_t *renderer_t, vk_shader_program_t *program);
void			 vk_renderer_clear_gl_errors (void);
bool			 vk_renderer_are_extensions_supported (const char *extensions[]);

static inline void
vk_renderer_reset (vk_renderer_t *renderer)
{
	renderer->reset (renderer);
}

static inline void
vk_renderer_delete (vk_renderer_t **renderer_)
{
	if (renderer_)
		(*renderer_)->delete (renderer_);
}

#endif /* __VK_REND_H__ */

