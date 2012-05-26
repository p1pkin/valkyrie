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

#ifndef __VK_HKGPU_H__
#define __VK_HKGPU_H__

#include "vk/buffer.h"
#include "vk/device.h"
#include "vk/renderer.h"

#include "mach/hikaru/hikaru.h"

vk_device_t	*hikaru_gpu_new (vk_machine_t *mach,
		                 vk_buffer_t *cmdram,
		                 vk_buffer_t *fb,
		                 vk_buffer_t *texram[2],
		                 vk_renderer_t *renderer);
void		 hikaru_gpu_vblank_out (vk_device_t *dev);
void		 hikaru_gpu_vblank_in (vk_device_t *dev);
void		 hikaru_gpu_hblank_in (vk_device_t *dev, unsigned line);
const char	*hikaru_gpu_get_debug_str (vk_device_t *dev);

#endif /* __VK_HKGPU_H__ */
