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

#include <stdio.h>
#include <string.h>

#include "vk/mmap.h"

void
vk_mmap_print (vk_mmap_t *mmap)
{
	unsigned i;
	VK_ASSERT (mmap);
	for (i = 0; mmap->regions[i]->mask != 0; i++)
		vk_region_print (mmap->regions[i]);
}

static inline unsigned
get_size_flag_for_size (unsigned size)
{
	switch (size) {
	case 1:
		return VK_REGION_SIZE_8;
	case 2:
		return VK_REGION_SIZE_16;
	case 4:
		return VK_REGION_SIZE_32;
	case 8:
		return VK_REGION_SIZE_64;
	}
	return 0;
}

vk_region_t *
vk_mmap_get_region (vk_mmap_t *mmap, uint32_t addr, unsigned flags)
{
	unsigned i;

	VK_ASSERT (mmap);
	VK_ASSERT ((flags & ~VK_REGION_RW) == 0);

	for (i = 0; mmap->regions[i]->mask != 0; i++) {
		vk_region_t *region = mmap->regions[i];
		if (addr >= region->lo && addr <= region->hi &&
		    (region->flags & flags))
			return region;
	}
	return NULL;
}

int
vk_mmap_get (vk_mmap_t *mmap, unsigned size, uint32_t addr, void *data)
{
	vk_region_t *region;

	VK_ASSERT (mmap != NULL);
	VK_ASSERT (data != NULL);
	VK_ASSERT (is_size_valid (size));

	region = vk_mmap_get_region (mmap, addr, VK_REGION_READ);
	if (!region || !(region->flags & get_size_flag_for_size (size)))
		return -1;

	if (region->flags & VK_REGION_LOG_READ && addr != 0x1A000018)
		VK_MACH_LOG (mmap->mach, "%s R%u %08X", region->name, size * 8, addr);

	if (region->flags & VK_REGION_NOP)
		return set_ptr (data, size, random ());

	if (region->flags & VK_REGION_DIRECT) {
		uint32_t offs = vk_region_offs (region, addr);
		uint64_t temp;
		temp = region->data.buffer->get (region->data.buffer, size, offs);
		set_ptr (data, size, temp);
		return 0;
	}

	return vk_device_get (region->data.device, size, addr, data);
}

int
vk_mmap_put (vk_mmap_t *mmap, unsigned size, uint32_t addr, uint64_t data)
{
	vk_region_t *region;

	VK_ASSERT (mmap != NULL);
	VK_ASSERT (is_size_valid (size));

	region = vk_mmap_get_region (mmap, addr, VK_REGION_WRITE);
	if (!region)
		return -1;

	if (!(region->flags & get_size_flag_for_size (size)))
		return -1;

	if (region->flags & VK_REGION_LOG_WRITE)
		VK_MACH_LOG (mmap->mach, "%s W%u %08X = %X", region->name, size * 8, addr, data);

	if (region->flags & VK_REGION_NOP)
		return 0;

	if (region->flags & VK_REGION_DIRECT) {
		uint32_t offs = vk_region_offs (region, addr);
		region->data.buffer->put (region->data.buffer, size, offs, data);
		return 0;
	}

	return vk_device_put (region->data.device, size, addr, data);
}

void
vk_mmap_set_region (vk_mmap_t *mmap, vk_region_t *region, unsigned index)
{
	mmap->regions[index] = region;
}

vk_mmap_t *
vk_mmap_new (vk_machine_t *mach, unsigned num)
{
	vk_mmap_t *mmap;

	VK_ASSERT (mach);
	VK_ASSERT (num);

	mmap = ALLOC (vk_mmap_t);
	if (!mmap)
		goto fail;

	mmap->regions = (vk_region_t **) calloc (num, sizeof (vk_region_t *));
	if (!mmap->regions)
		goto fail;

	mmap->mach = mach;

	return mmap;

fail:
	vk_mmap_delete (&mmap);
	return NULL;
}

void
vk_mmap_delete (vk_mmap_t **mmap_)
{
	if (mmap_) {
		vk_mmap_t *mmap = *mmap_;
		if (mmap) {
			FREE (mmap->regions);
			mmap->mach = NULL;
		}
		FREE (mmap_);
	}
}
