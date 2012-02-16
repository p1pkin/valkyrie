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

#ifndef __VK_REGION_H__
#define __VK_REGION_H__

#include "vk/buffer.h"
#include "vk/device.h"

#define VK_REGION_READ		(1 << 0)
#define VK_REGION_WRITE		(1 << 1)
#define VK_REGION_RW		(VK_REGION_READ|VK_REGION_WRITE)
#define VK_REGION_DIRECT	(1 << 2)
#define VK_REGION_NOP		(1 << 3)
#define VK_REGION_LOG_READ	(1 << 4)
#define VK_REGION_LOG_WRITE	(1 << 5)
#define VK_REGION_LOG_RW	(VK_REGION_LOG_READ|VK_REGION_LOG_WRITE)
#define VK_REGION_SIZE_8	(1 << 6)
#define VK_REGION_SIZE_16	(1 << 7)
#define VK_REGION_SIZE_32	(1 << 8)
#define VK_REGION_SIZE_64	(1 << 9)
#define VK_REGION_SIZE_ALL	(VK_REGION_SIZE_8|VK_REGION_SIZE_16|VK_REGION_SIZE_32|VK_REGION_SIZE_64)

typedef struct {
	uint32_t lo;
	uint32_t hi;
	uint32_t mask;
	unsigned flags;
	union {
		vk_buffer_t *buffer;
		vk_device_t *device;
	} data;
	char name[16];
} vk_region_t;

static const vk_region_t vk_region_end = {
	0, 0, 0, 0, { 0 }, "<END>"
};

#define VK_REGION_IS_END(region_) \
	((region_)->mask == 0)

vk_region_t	*vk_region_nop_new (uint32_t lo, uint32_t hi, uint32_t mask, unsigned flags, const char *name);
vk_region_t	*vk_region_rom_new (uint32_t lo, uint32_t hi, uint32_t mask, unsigned flags, vk_buffer_t *buffer, const char *name);
vk_region_t	*vk_region_ram_new (uint32_t lo, uint32_t hi, uint32_t mask, unsigned flags, vk_buffer_t *buffer, const char *name);
vk_region_t	*vk_region_mmio_new (uint32_t lo, uint32_t hi, uint32_t mask, unsigned flags, vk_device_t *device, const char *name);
void		 vk_region_delete (vk_region_t **region_);
void		 vk_region_print (vk_region_t *region);

static inline uint32_t
vk_region_offs (vk_region_t *region, uint32_t addr)
{
	return addr & region->mask;
}

#endif /* __VK_REGION_H__ */
