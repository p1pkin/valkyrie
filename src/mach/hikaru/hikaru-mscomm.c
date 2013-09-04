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

#include "vk/buffer.h"
#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-mscomm.h"

/*
 * Master/Slave Communication Box
 * ==============================
 *
 * Not sure how the comm box works exactly; does it imply any kind of IRQ?
 * Does it allow to start/reset the slave (kind of like the Saturn SMPC)?
 *
 * MMIOs
 * =====
 *
 * Master   Slave
 * -------- --------
 * 14000000 10000000	Unknown; Control? Possibly 0 turns off/resets the slave (NMI?); anything else turns it on
 * /        /
 * 14000008 10000008	Box Port 0
 * 1400000C 1000000C	Box Port 1
 * 14000010 10000010	Box Port 2
 * 14000014 10000014	Box Port 3
 * /        /
 * 1400002E 1000002E	Unknown; 3 is written in __slave_init and read from the master
 */

typedef struct {
	vk_device_t base;
	vk_buffer_t *regs;
} hikaru_mscomm_t;

/* Note: access from master will have addr = 140000xx, from slave = 100000xx */

static int
hikaru_mscomm_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_mscomm_t *comm = (hikaru_mscomm_t *) dev;

	set_ptr (val, size, vk_buffer_get (comm->regs, size, addr & 0x3F));
	switch (addr & 0xFF) {
	case 0x00:
	case 0x08:
	case 0x0C:
	case 0x10:
	case 0x14:
		VK_ASSERT (size == 4);
		break;
	case 0x2E:
		VK_ASSERT (size == 2);
		break;
	default:
		return -1;
	}
	return 0;
}

static int
hikaru_mscomm_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_mscomm_t *comm = (hikaru_mscomm_t *) dev;
	switch (addr & 0xFF) {
	case 0x00:
	case 0x08:
	case 0x0C:
	case 0x10:
	case 0x14:
	case 0x20:
	case 0x24:
	case 0x28:
		VK_ASSERT (size == 4);
		break;
	case 0x2E:
		VK_ASSERT (size == 2);
		break;
	default:
		return -1;
	}
	vk_buffer_put (comm->regs, size, addr & 0x3F, val);
	return 0;
}

static int
hikaru_mscomm_exec (vk_device_t *dev, int cycles)
{
	return -1;
}

static void
hikaru_mscomm_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_mscomm_t *comm = (hikaru_mscomm_t *) dev;
	vk_buffer_clear (comm->regs);
}

static int
hikaru_mscomm_save_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static int
hikaru_mscomm_load_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static void
hikaru_mscomm_destroy (vk_device_t **dev_)
{
	hikaru_mscomm_t *comm = (hikaru_mscomm_t *) *dev_;

	if (comm)
		vk_buffer_destroy (&comm->regs);

	free (comm);
	*dev_ = NULL;
}

vk_device_t *
hikaru_mscomm_new (vk_machine_t *mach)
{
	hikaru_mscomm_t *comm;
	vk_device_t *dev;

	VK_DEVICE_ALLOC (comm, mach);
	dev = (vk_device_t *) comm;
	if (!comm)
		return NULL;

	dev->destroy	= hikaru_mscomm_destroy;
	dev->reset	= hikaru_mscomm_reset;
	dev->exec	= hikaru_mscomm_exec;
	dev->get	= hikaru_mscomm_get;
	dev->put	= hikaru_mscomm_put;
	dev->save_state	= hikaru_mscomm_save_state;
	dev->load_state	= hikaru_mscomm_load_state;

	comm->regs = vk_buffer_le32_new (0x40, 0);
	if (!comm->regs)
		goto fail;

	vk_machine_register_buffer (mach, comm->regs);

	return dev;

fail:
	vk_device_destroy (&dev);
	return NULL;
}
