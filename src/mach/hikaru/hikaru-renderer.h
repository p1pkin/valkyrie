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
#include "vk/buffer.h"
#include "vk/renderer.h"

vk_renderer_t	*hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2]);
void		 hikaru_renderer_set_gpu (vk_renderer_t *, void *);

#endif /* __VK_HIKARU_RENDERER_H__ */
