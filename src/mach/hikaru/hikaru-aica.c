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
 *  800060	Unknown		   See @0C0060D8
 *  800090	5D800000 | address \
 *  800094	5D800000 | address | See @0C006000
 *  800098	Length             /
 *  8000B0
 *  800400
 *  800500
 */

#if 0
		log = true;
		switch (bus_addr & 0xFFFFFF) {
		case 0x0080005C: {
				static uint32_t hack = 0;
				hack ^= 1;
				set_ptr (val, size, hack);
			}
			break;
		case 0x00800060 ... 0x0080007F:
			set_ptr (val, size, 0xFFFFFFFF);
			break;
		}
#endif

typedef struct {
	vk_device_t base;
	vk_buffer_t *ram;
	bool master;
} hikaru_aica_t;

static int
hikaru_aica_get (vk_device_t *device, unsigned size, uint32_t addr, void *val)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) device;
	(void) aica;
	return -1;
}

static int
hikaru_aica_put (vk_device_t *device, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_aica_t *aica = (hikaru_aica_t *) device;
	(void) aica;
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
