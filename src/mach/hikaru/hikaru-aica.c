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

#include "mach/hikaru/hikaru-aica.h"

/*
 * Sound Boards
 * ============
 *
 * Apparently SNDBD and SNDBD2 are identical, except that the latter is
 * optional.
 *
 * 700000-701FFF AICA Channels 0-63. Each slot is 128 bytes long.
 * 702000-7027FF AICA ESF (?)
 * 702800-7028BD AICA Global (?)
 * 702C00        ARM Reset
 * 702D00        AICA IRQ L
 * 702D04        AICA IRQ R
 * 703000-7031FF COEF
 * 703200-7033FF MADRS
 * 703400-703BFF MPRO; 3BFE --> AICA DSP start
 *
 * 710000 RTC Lo
 * 710004 RTC Hi
 * 710008 RTC Write Enable
 *
 * 800000-FFFFFF RAM
 *
 * Note: this documentation comes from the following sources: MAME,
 * lxdream, nullDC. Kudos to the original authors.
 */

typedef struct {
	vk_device_t base;
	vk_buffer_t *ram;
	uint32_t rtc[4];
	bool master;
} hikaru_aica_t;

static int
hikaru_aica_get (vk_device_t *device, unsigned size, uint32_t addr, void *val)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) device;
	uint32_t *val32 = (uint32_t *) val;
	uint32_t offs = addr & 0xFFFFFF;

	VK_MACH_LOG (device->mach, "AICA/%c R%u @%08X",
	             aica->master ? 'M' : 'S', 8*size, offs);

	switch (offs) {
	case 0x702C00:
		/* ARM Reset */
		return 0;
	case 0x710000:
	case 0x710004:
	case 0x710008:
		/* AICA RTC */
		VK_ASSERT (size == 4);
		*val32 = aica->rtc[(offs & 0xF) / 4];
		return 0;
	}
	return -1;
}

static int
hikaru_aica_put (vk_device_t *device, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) device;
	uint32_t offs = addr & 0xFFFFFF;

	VK_MACH_LOG (device->mach, "AICA/%c W%u @%08X = %X",
	             aica->master ? 'M' : 'S', 8*size, offs, val);

	switch (offs) {
	case 0x702800:
		/* Master Volume */
	case 0x702C00:
		/* ARM Reset */
		return 0;
	case 0x710000:
	case 0x710004:
	case 0x710008:
		/* AICA RTC */
		VK_ASSERT (size == 4);
		return 0;
	}
	return -1;
}

static int
hikaru_aica_exec (vk_device_t *dev)
{
	return -1;
}

static void
hikaru_aica_reset (vk_device_t *dev, vk_reset_type_t type)
{
}

static int
hikaru_aica_load_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static int
hikaru_aica_save_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static void
hikaru_aica_delete (vk_device_t **dev_)
{
	/* TODO */
}

vk_device_t *
hikaru_aica_new (vk_machine_t *mach, vk_buffer_t *ram, bool master)
{
	hikaru_aica_t *aica = ALLOC (hikaru_aica_t);
	vk_device_t *device = (vk_device_t *) aica;
	if (aica) {
		aica->base.mach = mach;

		aica->base.delete	= hikaru_aica_delete;
		aica->base.reset	= hikaru_aica_reset;
		aica->base.exec		= hikaru_aica_exec;
		aica->base.get		= hikaru_aica_get;
		aica->base.put		= hikaru_aica_put;
		aica->base.save_state	= hikaru_aica_save_state;
		aica->base.load_state	= hikaru_aica_load_state;

		/* XXX register ARM7 processor in the main loop */

		aica->ram = ram;
		aica->master = master;
	}

	return device;
fail:
	hikaru_aica_delete (&device);
	return NULL;
}
