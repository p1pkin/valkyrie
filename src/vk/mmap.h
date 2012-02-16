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

/* XXX this API sucks; we really need vk_list_t */

typedef struct {
	vk_region_t **regions;
	vk_machine_t *mach;
} vk_mmap_t;

vk_mmap_t	*vk_mmap_new (vk_machine_t *mach, unsigned num);
void		 vk_mmap_delete (vk_mmap_t **mmap_);
void		 vk_mmap_set_region (vk_mmap_t *mmap, vk_region_t *region, unsigned index);
int		 vk_mmap_get (vk_mmap_t *mmap, unsigned size, uint32_t addr, void *data);
int		 vk_mmap_put (vk_mmap_t *mmap, unsigned size, uint32_t addr, uint64_t data);

#endif /* __VK_MMAP_H__ */
