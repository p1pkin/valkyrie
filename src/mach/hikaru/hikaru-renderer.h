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

#ifndef __VK_HIKARU_RENDERER_H__
#define __VK_HIKARU_RENDERER_H__

#include "vk/core.h"
#include "vk/types.h"
#include "vk/buffer.h"
#include "vk/surface.h"
#include "vk/renderer.h"

typedef struct {
	vk_renderer_t base;

	vk_buffer_t *texram;
	vk_surface_t *texture;

} hikaru_renderer_t;

vk_renderer_t	*hikaru_renderer_new (vk_buffer_t *texram);
void		 hikaru_renderer_draw_tri (hikaru_renderer_t *renderer,
		                           vec3f_t *v0, vec3f_t *v1, vec3f_t *v2,
		                           bool has_color,
		                           vec4b_t color,
		                           bool has_texture,
		                           vec2s_t *uv0, vec2s_t *uv1, vec2s_t *uv2);
void		hikaru_renderer_draw_layer (hikaru_renderer_t *renderer,
		                            vec2f_t coords[4]);

#endif /* __VK_HIKARU_RENDERER_H__ */
