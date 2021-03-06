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

#include "vk/input.h"

#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-mie.h"

/*
 * MIE
 * ===
 *
 * Includes a Z80 processor clocked at ? MHz.
 *
 * MME Ports
 * =========
 *
 * 00800000  16-bit RW	Unknown \ Related
 * 00800002  16-bit  W	Unknown /
 * 00800004  16-bit  W	Unknown; = 0000
 * 00800006  16-bit RW	MIE Control
 *			bit 1 is accessed in @0C00BDFC; turn ON/OFF [active low]
 *			bit 5 is accessed in @0C007880, @0C002AB8; lock/unlock BRAM? see @0C002C4A
 *			bit 8 is accessed in @0C0078F8, related to hi/lo-res video mode
 * 00800008  16-bit RW	Unknown
 *			bits 0-3 are read (and looped if == 0) in @0C00BDFC
 *			Some kind of ready bits
 * 0080000A  16-bit RW	Serial Port; = 007F
 * 0080000C  16-bit RW	Serial Port; = FFFF; plus MAINBD Switches
 * 00800010  16-bit  W	Unknown; = 0043
 *
 * Basic setup is done in @0C001956. Most bits seem active-low, with the
 * exception of 00800004. Access to register is guarded either by 01000000 or
 * 01000100.
 *
 * 0082F000   8-bit RW	MIE Z80 Control; 0x80 is the Z80 reset/start bit
 *
 * 00830000-0083FFFF  8,16-bit RW	MIE RAM: even bytes only [2]
 *
 * Note: airtrix polls 0083800[02]
 *
 * [1] 7800 in Z80 space
 * [2] 8000-FFFF in Z80 space
 *
 * MIE Serial Bus
 * ==============
 *
 * Perhaps queries devices attached to the (MAPLE?) bus?
 */

typedef struct {
	vk_device_t base;
	vk_buffer_t *regs;
	bool hack;
} hikaru_mie_t;

static uint16_t get_mainbd_switches (void)
{
	return (vk_input_get_key (SDLK_F5) ? 8 : 0) | /* Test */
	       (vk_input_get_key (SDLK_F6) ? 4 : 0);  /* Service */
}

static int
hikaru_mie_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;

	set_ptr (val, size, 0);
	if (addr >= 0x00800000 && addr <= 0x00800014) {
		uint16_t *val16 = (uint16_t *) val, tmp;
		if (size != 2)
			return -1;
		switch (addr & 0xFF) {
		case 0x00:
		case 0x06:
		case 0x0A:
			/* no-op */
			break;
		case 0x08:
			/* XXX hack: passes the check at @0C00B860 in all the
			 * BOOTROM versions */
			tmp = vk_buffer_get (mie->regs, 2, 0x08);
			vk_buffer_put (mie->regs, 2, 0x08, tmp ^ 0xF);
			break;
		case 0x0C:
			vk_buffer_put (mie->regs, 2, 0x0C, 0xFFFF & ~get_mainbd_switches ());
			break;
		default:
			return -1;
		}
		*val16 = vk_buffer_get (mie->regs, 2, addr & 0x1F);
	} else if (addr == 0x0082F000) {
		return 0;
	} else if (addr >= 0x00830000 && addr <= 0x0083FFFF) {
		/* FIXME handle size != 1 */
		/*set_ptr (val, size, vk_buffer_get (((hikaru_t *) dev->mach)->mie_ram, size, (addr / 2) & 0x7FFF));*/
		set_ptr (val, size, 0);
		if (mie->hack) {
			/* XXX hack: fakes MIE better, but then the games
			 * don't poll the MAINBD switches anymore. See
			 * AT:@0C69B34E */
			if (addr == 0x00838004)
				set_ptr (val, size, 3);
			else if (addr == 0x00838008)
				set_ptr (val, size, 6);
		}
	} else
		return -1;
	return 0;
}

static int
hikaru_mie_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	if (addr >= 0x00800000 && addr <= 0x00800014) {
		if (size != 2)
			return -1;
		switch (addr & 0xFF) {
		case 0x00:
		case 0x02:
		case 0x04:
		case 0x06:
		case 0x08:
		case 0x0A:
		case 0x0C:
		case 0x10:
			break;
		default:
			return -1;
		}
	} else if (addr == 0x0082F000) {
		return 0;
	} else if (addr >= 0x00830000 && addr <= 0x0083FFFF) {
		/* FIXME size */
		vk_buffer_put (((hikaru_t *) dev->mach)->mie_ram, size, (addr / 2) & 0x7FFF, val);
	} else
		return -1;
	return 0;
}

static void
hikaru_mie_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_mie_t *mie = (hikaru_mie_t *) dev;

	vk_buffer_clear (mie->regs);
	vk_buffer_put (mie->regs, 2, 0x00, 0xFFFF);

	mie->hack = vk_util_get_bool_option ("MIE_HACK", false);
}

vk_device_t *
hikaru_mie_new (vk_machine_t *mach)
{
	hikaru_mie_t *mie;
	vk_device_t *dev;

	VK_DEVICE_ALLOC (mie, mach);
	dev = (vk_device_t *) mie;
	if (!mie)
		return NULL;

	dev->destroy	= NULL;
	dev->reset	= hikaru_mie_reset;
	dev->exec	= NULL;
	dev->get	= hikaru_mie_get;
	dev->put	= hikaru_mie_put;
	dev->save_state	= NULL;
	dev->load_state	= NULL;

	mie->regs = vk_buffer_le32_new (0x20, 0);
	if (!mie->regs)
		goto fail;

	vk_machine_register_buffer (mach, mie->regs);

	return dev;

fail:
	vk_device_destroy (&dev);
	return NULL;
}
