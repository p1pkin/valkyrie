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
#include "vk/renderer.h"
#include "mach/hikaru/hikaru-gpu-private.h"

vk_renderer_t	*hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2]);

/* 2D */

void		hikaru_renderer_draw_layer (vk_renderer_t *renderer,
		                            hikaru_gpu_layer_t *layer);

/* 3D */

void		hikaru_renderer_set_viewport (vk_renderer_t *renderer,
		                              hikaru_gpu_viewport_t *viewport);
void		hikaru_renderer_set_matrix (vk_renderer_t *renderer,
		                            mtx4x4f_t mtx);
void		hikaru_renderer_set_material (vk_renderer_t *renderer,
 		                              hikaru_gpu_material_t *material);
void		hikaru_renderer_set_texhead (vk_renderer_t *renderer,
		                             hikaru_gpu_texhead_t *texhead);
void		hikaru_renderer_set_lightset (vk_renderer_t *renderer,
		                              hikaru_gpu_lightset_t *lightset,
		                              uint32_t enabled_mask);
void		hikaru_renderer_set_modelview_vector (vk_renderer_t *renderer,
		                                      unsigned m, unsigned n,
		                                      vec3f_t vector);
void		hikaru_renderer_add_vertex (vk_renderer_t *renderer,
		                            vec3f_t pos);
void		hikaru_renderer_add_texcoords (vk_renderer_t *renderer,
		                               vec2f_t texcoords[3]);
void		hikaru_renderer_add_vertex_full (vk_renderer_t *renderer,
		                                 vec3f_t pos,
		                                 vec3f_t normal,
		                                 vec2f_t texcoords);
void		hikaru_renderer_end_mesh (vk_renderer_t *renderer);

#endif /* __VK_HIKARU_RENDERER_H__ */
