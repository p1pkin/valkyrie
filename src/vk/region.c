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

#include "vk/region.h"

static const char * const flag_str[] = {
	"R", "W", "DIR", "NOP", "LR", "LW", "1", "2", "4", "8",
};

static const char *
region_flags_to_str (vk_region_t *region)
{
	static char out[256] = "TODO";
	/* XXX TODO */
	return out;
}

static const char *
region_to_str (vk_region_t *region)
{
	static char out[256];
	sprintf (out, "[%08X-%08X (%08X) '%s' flags=%s",
	         region->lo, region->hi, region->mask, region->name,
	         region_flags_to_str (region));
	return out;
}

void
vk_region_print (vk_region_t *region)
{
	if (!region)
		printf ("[ null region ]\n");
	else
		printf ("[ region: %s ]\n", region_to_str (region));
}

void
vk_region_delete (vk_region_t **region_)
{
	if (region_) {
		vk_region_t *region = *region_;
		if (region && (region->flags & VK_REGION_DIRECT))
			vk_buffer_delete (&region->data.buffer);
		free (region);
		*region_ = NULL;
	}
}

static vk_region_t *
vk_region_new (uint32_t lo, uint32_t hi, uint32_t mask,
               unsigned flags, const char *name)
{
	vk_region_t *region;

	VK_ASSERT (flags & VK_REGION_RW);
	VK_ASSERT (flags & VK_REGION_SIZE_ALL);
	VK_ASSERT (name);

	region =  ALLOC (vk_region_t);
	if (!region)
		return NULL;

	region->lo = lo;
	region->hi = hi;
	region->mask = mask;
	region->flags = flags;

	strncpy (region->name, name, 15);
	region->name[15] = '\0';

	return region;
}

vk_region_t *
vk_region_nop_new (uint32_t lo, uint32_t hi, uint32_t mask,
                   unsigned flags, const char *name)
{
	VK_ASSERT (!(flags & VK_REGION_DIRECT));
	VK_ASSERT (flags & VK_REGION_RW);

	flags |= VK_REGION_NOP;
	return vk_region_new (lo, hi, mask, flags, name);
}

vk_region_t *
vk_region_rom_new (uint32_t lo, uint32_t hi, uint32_t mask,
                   unsigned flags, vk_buffer_t *buffer,
                   const char *name)
{
	vk_region_t *region;

	VK_ASSERT (buffer);
	VK_ASSERT (!(flags & VK_REGION_WRITE))

	flags |= VK_REGION_DIRECT | VK_REGION_READ | VK_REGION_SIZE_ALL;
	region = vk_region_new (lo, hi, mask, flags, name);
	if (region)
		region->data.buffer = buffer;
	return region;
}

vk_region_t *
vk_region_ram_new (uint32_t lo, uint32_t hi, uint32_t mask,
                   unsigned flags, vk_buffer_t *buffer,
                   const char *name)
{
	vk_region_t *region;

	VK_ASSERT (buffer);

	flags |= VK_REGION_DIRECT | VK_REGION_RW | VK_REGION_SIZE_ALL;
	region = vk_region_new (lo, hi, mask, flags, name);
	if (region)
		region->data.buffer = buffer;
	return region;
}

vk_region_t *
vk_region_mmio_new (uint32_t lo, uint32_t hi, uint32_t mask,
                    unsigned flags, vk_device_t *device, const char *name)
{
	vk_region_t *region;

	VK_ASSERT (!(flags & VK_REGION_DIRECT));
	VK_ASSERT (device);

	region = vk_region_new (lo, hi, mask, flags, name);
	if (region) {
		region->data.device = device;
	}
	return region;
}
