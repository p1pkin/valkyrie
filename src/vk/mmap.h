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

#ifndef __VK_MMAP_H__
#define __VK_MMAP_H__

#include "vk/region.h"
#include "vk/vector.h"

#define VK_REGION_R		(1 << 0)
#define VK_REGION_W		(1 << 1)
#define VK_REGION_RW		(VK_REGION_R|VK_REGION_W)

#define VK_REGION_DIRECT	(1 << 2)

#define VK_REGION_LOG_R		(1 << 4)
#define VK_REGION_LOG_W		(1 << 5)
#define VK_REGION_LOG_RW	(VK_REGION_LOG_R|VK_REGION_LOG_W)

#define VK_REGION_SIZE_8	(1 << 6)
#define VK_REGION_SIZE_16	(1 << 7)
#define VK_REGION_SIZE_32	(1 << 8)
#define VK_REGION_SIZE_64	(1 << 9)
#define VK_REGION_SIZE_ALL	(VK_REGION_SIZE_8|VK_REGION_SIZE_16|VK_REGION_SIZE_32|VK_REGION_SIZE_64)

typedef struct {
	vk_vector_t *regions;
	vk_machine_t *mach;
} vk_mmap_t;

vk_mmap_t	*vk_mmap_new (vk_machine_t *mach);
void		 vk_mmap_destroy (vk_mmap_t **mmap_);
int		 vk_mmap_add_ram (vk_mmap_t *mmap, uint32_t lo, uint32_t hi,
		                  uint32_t mask, uint32_t flags,
		                  vk_buffer_t *buf, const char *name);
int		 vk_mmap_add_rom (vk_mmap_t *mmap, uint32_t lo, uint32_t hi,
		                  uint32_t mask, uint32_t flags,
		                  vk_buffer_t *buf, const char *name);
int		 vk_mmap_add_dev (vk_mmap_t *mmap, uint32_t lo, uint32_t hi,
		                  uint32_t mask, uint32_t flags,
		                  vk_device_t *dev, const char *name);
int		 vk_mmap_get (vk_mmap_t *mmap, unsigned size, uint32_t addr, void *data);
int		 vk_mmap_put (vk_mmap_t *mmap, unsigned size, uint32_t addr, uint64_t data);

#endif /* __VK_MMAP_H__ */
