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

#include "vk/core.h"

#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-memctl.h"

/* TODO: handle the memctl apertures with manipulating the hikaur's mmaps
 * directly. */

/*
 * Memory Controller
 * =================
 *
 * TODO.
 *
 * MMIO Ports
 * ==========
 *
 *  Offset	+3       +2       +1       +0
 *  +0x00	IIIIIIII IIIIIIII IIIIIIII IIIIIIII	Controller ID
 *  +0x04	---u---- -------- ---sEEEE EEFFFFFF	DMA Status
 *  +0x08	-------- -------- -------- --------
 *  +0x0C	-------- -------- -------- --------
 *  +0x10	dddddddd cccccccc bbbbbbbb aaaaaaaa	Aperture 0 Control
 *  +0x14	hhhhhhhh gggggggg ffffffff eeeeeeee	Aperture 1 Control
 *  +0x18	llllllll kkkkkkkk jjjjjjjj iiiiiiii	Aperture 2 Control
 *  +0x1C	pppppppp oooooooo nnnnnnnn mmmmmmmm	Aperture 3 Control ?
 *  +0x20	tttttttt ssssssss rrrrrrrr qqqqqqqq	Unknown
 *  +0x24	xxxxxxxx wwwwwwww vvvvvvvv uuuuuuuu	Unknown
 *  +0x28	-------- -------- -------- --------
 *  +0x2C	-------- -------- -------- --------
 *  +0x30	DDDDDDDD DDDDDDDD DDDDDDDD DDD-----	DMA Destination Address
 *  +0x34	SSSSSSSS SSSSSSSS SSSSSSSS SSS-----	DMA Source Address
 *  +0x38	-------C LLLLLLLL LLLLLLLL LLLLLLLL	DMA Control
 *  +0x3C	-------- -------- -------- XXXXXXXX	Unknown
 *
 * Fields	Meaning			Values			References
 * -----------------------------------------------------------------------
 * +0x00	I = ID			 0 = Master		@0C00B88C
 *					~0 = Slave
 * +0x04	u = Unknown		1			@0C0016A4
 *		s = DMA status					@0C001728
 *		E = BUS error bits, Master			@0C001988
 *		F = BUS error bits, Slave			@0C001CC4
 * +0x10	a = Controls 14xxxxxx	48 [m]			@0C0016A4
 *					00 [s]			@0C001CC4
 *		b = Controls 15xxxxxx?	00 [m]			@0C0016A4
 *					40 [m]			@0C00BDFC
 *		c = Controls 16xxxxxx	40 [m]			@0C0016A4
 *					02 [m]			@0C00BDFC
 *		d = Controls 17xxxxxx	41 [m]			@0C0016A4
 *					04 [m]			@0C001C70
 *					06 [m]			@0C001C70
 *					03 [m]			@0C00BDFC
 * +0x14	e = Unknown		C0 [m]			@0C0016A4
 *					E6 [s]			@0C00BE70
 *					70 [s] !!!!!!!!!!!
 *		f = Unknown		C1 [m]			@0C0016A4
 *					EE [s]			@0C00BE70
 *		g = Unknown		F2 [m]			@0C0016A4
 *					C2 [m]			@0C00BDFC
 *					F4 [s]			@0C00BE70
 *		h = Unknown		F3 [m]			@0C0016A4
 *					C3 [m]			@0C00BDFC
 *					CC [s]			@0C00BE70
 * +0x18	i = Controls 00xxxxxx ? AICA IRL in the old docs
 *		j = Controls 01xxxxxx ?
 *		k = Controls 02xxxxxx	01			@0C0016A4
 *					0A = SNDBD		@0C001F3C
 *		l = Controls 03xxxxxx	10 = EPROM		@0C007964
 *					...
 *					1B = EPROM
 *
 * +0x1C	m = Controls 18xxxxxx[m]			@0C001CC4
 *		    Controls 14xxxxxx[s]	00,01
 *		n = Unknown
 *		o = Unknown			01 [m]		@0C00BDFC
 *						01 [s]		@0C00BE70
 *		p = Unknown
 * +0x20	q = Unknown			FE [m]		@0C00xxxx, @0C00BDFC
 *		r = Unknown			00 [m]
 *		s = Unknown			FE [m]
 *		t = Unknown			00 [m]
 * +0x24	u = Unknown			E6 [m]
 *		v = Unknown			5E [m]
 *						EE [m] MIE	@0C00BDFC
 *		w = Unknown			F4 [m] (E4, B4, F4 while accessing banks D, E in AIRTRIX)
 *						FD [m] SNDBD	@0C001F3C, @oCooBDFC
 *		x = Unknown			CC [m]		@0C00BDFC, @0C007820 => NIBBLES
 * +0x3x	D = DMA destination address
 * +0x34	S = DMA source address
 * +0x38	C = DMA begin/busy
 *		L = DMA transfer length in 32-bit words
 *		    See @0C008640
 * +0x3C	X = 0C to access the SNDBD1:027028BC
 *		    A2 to access the SNDBD2:027028BC
 *		    See @0C001748
 *
 * Note: other interesting evidence is at PH:@0C0124B8, which translates all
 * addresses which are set to banks:
 *
 *  0x18 = YYxxxxxx  --->  returns 0xA0xxxxxx
 *  0x19 = YYxxxxxx  --->  returns 0xA1xxxxxx
 *  0x1A = YYxxxxxx  --->  returns 0xA2xxxxxx
 *  0x1B = YYxxxxxx  --->  returns 0xA3xxxxxx
 *
 * Which makes perfect sense as far as A2 and A3 go (the ROMBD area).
 *
 * Note: accessing 3C may alter other registers; for instance, the code at
 * @0C001748  saves/restores 04000018 before accessing 0400003C.
 *
 * Accessing the bus (apertures) may give rise to errors, both during DMA
 * operation and during normal access; these errors get reported in E and F.
 *
 *
 * DMA Operation
 * =============
 *
 * The DMA is likely used to transfer data from the main RAM to devices on
 * different boards (ROMBD, SNDBD, SNDBD2, NETBD). The address space however
 * is different from that of the SH-4 CPUs, and is still largely unknown.
 *
 * 0x70000000-0x72000000	RAM [m]	Note 0x70 - 0x60 = 0x10, makes no sense
 *
 * The addresses here may be related to the addresses computed in @0C006608.
 *
 * Setting c to 1 initiates the DMA operation. After completion, the MEMCTL
 * raises IRL 1 on the master SH-4, and sets bit 0x1000 in 04000004 and its
 * corresponding error field.
 *
 * See @0C008640 for more details XXX
 *
 *
 * External Boards
 * ===============
 *
 * The hikaru mainboard is connected to a number of external boards:
 *  - the ROM board (ROMBD)
 *  - the sound board
 *  - an optional sound board
 *  - an optional network board
 *
 * Access to these boards is performed through the region at 02000000-03FFFFFF,
 * a 32 MB aperture. What hardware is currently mapped at the aperture is
 * decided by the memory controller at 04000000. The two 16MB halves are
 * mapped independently of each other. There may be more than one aperture
 * in the memory map.
 *
 * Known mapping registers are: 04000010, 04000014, 04000018. Each byte maps
 * a whole 16MB range.
 *
 * See @0C006608 for more information on the mapping between the bank and the
 * affected memory range. Apparently when setting:
 *
 *	[0400001n + aperture & 3] = (addr_to_be_mapped >> 24)
 *
 * the resulting address is ((aperture - 0x60) << 24) | offset
 *
 *
 * External Bus Memory Map
 * =======================
 *
 * 04000000-043FFFFF	Unknown; GPU-related 4MB Area; Frame Buffer 1?
 * 06000000-063FFFFF	Unknown; GPU-related 4MB Area; Frame Buffer 2?
 * 0A000000-0A00FFFF	Unknown; IO-related?
 * 0C000000-0CFFFFFF	Sound Board 1
 * 0D000000-0DFFFFFF	Sound Board 2
 * 0E000000-0E00FFFF	Network Board
 * 10000000-3FFFFFFF	ROMBD (EPROM, MASKROM, EEPROM get mapped here)
 * 40000000-41FFFFFF	Slave RAM
 * 48000000-483FFFFF	GPU CMDRAM
 * 70000000-71FFFFFF	Master RAM
 * 90000000-91FFFFFF	EPROM (?)
 * A0000000-A1FFFFFF	MASKROM (?)
 *
 *
 * Rom Board (ROMBD)
 * =================
 *
 * The ROMBD EPROMs and Mask ROMs are accessible through the memctl bus, at
 * the 02xxxxxx and 03xxxxxx apertures.
 *
 * The bootrom tries to figure out whether the EPROM is a 16MB one (up to 4
 * x 4MB EPROMs). The actual position of the ROMBD EEPROM serial device
 * depends on the address of the EPROMs and MASKROMs. The bootrom looks for
 * the 'SAMURA' ASCII string at both 03000000 and 0340000x.
 *
 *  The EPROMs can be mapped in banks 0x10-0x13 or 0x18-0x1B. The BootROM checks
 *  whether the 'SAMURAI' string appears at offset +0 at any of these locations
 *  (mapped at 0x2xxxxxxx and 0x3xxxxxxx through the memory controller.)
 * 
 *  If the 'SAMURAI' tag is not found, then:
 *   - the EEPROM is found at bank 0x14
 * 
 *  If the 'SAMURAI' tag is found in bank 0x10, then:
 *   - the EEPROM is found at bank 0x14
 *   - the MaskROMs are found at bank 0x20 onwards
 * 
 *  If the 'SAMURAI' tag is found in bank 0x10, then:
 *   - the EEPROM is found at bank 0x1C
 *   - the MaskROMs are found at bank 0x30 onwards
 * 
 *  Other banks seem not to be affected by the presence/offset of the ROMBD (which
 *  makes sense, since the HW mapped to those banks is not _physically_ on the
 *  ROMBD.)
 * 
 *  The ROMs are also checked for masking (I think) at offset +4MB, possibly to
 *  check whether the EPROMs are 2MB+2MB (as for braveff.)
 *
 * Bit 25 of the bus address may be some kind of magic 'this address space is
 * actually half the width you thought and every real byte is mirrored in a
 * 16-bitword' bit. See @0C00B938. This may also be related to the fact that
 * MASKROMs are (likely) hosted at bank 0x20+ of the bus.
 *
 * For a lot of interesting details, see @0C004ED2 (rombd_test)
 */

typedef struct {
	vk_device_t base;

	vk_buffer_t *regs;
	bool master;
} hikaru_memctl_t;

static uint32_t
get_bank_for_addr (hikaru_memctl_t *memctl, uint32_t addr)
{
	uint32_t reg;
	switch (addr >> 24) {
	case 0x02:
		reg = 0x1A;
		break;
	case 0x03:
		reg = 0x1B;
		break;
	case 0x16:
		reg = 0x12;
		break;
	case 0x17:
		reg = 0x13;
		break;
	case 0x18:
		reg = 0x1C;
		break;
	default:
		return 0;
	}
	return vk_buffer_get (memctl->regs, 1, reg);
}

/* TODO modify the hikaru->mmap_[ms] instead */

/* TODO raise m/s bus error on bad access */

static int
hikaru_memctl_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_memctl_t *memctl = (hikaru_memctl_t *) dev;
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	uint32_t bank, offs, bus_addr;

	if (addr >= 0x04000000 && addr <= 0x0400003F) {
		/* MMIOs */
		set_ptr (val, size, vk_buffer_get (memctl->regs, size, addr & 0x3F));
		return 0;
	}

	bank = get_bank_for_addr (memctl, addr);
	if (!bank)
		return -1;

	offs = addr & 0xFFFFFF;
	bus_addr = (bank << 24) | offs;

	set_ptr (val, size, 0);
	if (bus_addr >= 0x04000000 && bus_addr <= 0x043FFFFF) {
		/* Unknown A */
		set_ptr (val, size, vk_buffer_get (hikaru->unkram[0], size, offs));
	} else if (bus_addr >= 0x06000000 && bus_addr <= 0x063FFFFF) {
		/* Unknown B */
		set_ptr (val, size, vk_buffer_get (hikaru->unkram[1], size, offs));
	} else if (bus_addr >= 0x0A000000 && bus_addr <= 0x0A00FFFF) {
		/* Unknown */
	} else if (bus_addr >= 0x0C000000 && bus_addr <= 0x0CFFFFFF) {
		/* AICA 1 */
	} else if (bus_addr >= 0x0D000000 && bus_addr <= 0x0DFFFFFF) {
		/* AICA 2 */
	} else if (bus_addr >= 0x0E000000 && bus_addr <= 0x0E00FFFF) {
		/* Network Board */
		/* XXX likely m68k code */
	} else if (bus_addr >= 0x10000000 && bus_addr <= 0x3FFFFFFF) {
		/* ROMBD */

		/* Access here is valid even if performed on the wrong banks:
		 * we set the ptr to garbage here because the hikaru bootrom
		 * reads indiscriminately from banks 10-1B (including the
		 * EPROM bank!) to infer/compute the EPROM format. We
		 * don't want spurious matchings of 0 vs 0 to affect the
		 * computation. */
		set_ptr (val, size, rand ());

		if (bank == hikaru->eeprom_bank && offs == 0) {
			/* ROMBD EEPROM */
			set_ptr (val, size, 0);
		} else if (!hikaru->has_rom) {
			return 0;
		} else if (bank >= hikaru->eprom_bank[0] &&
		           bank <= hikaru->eprom_bank[1]) {
			/* ROMBD EPROM */
			/* XXX no idea if mirroring should occur within 4MB
			 * sub-buffers */
			set_ptr (val, size, vk_buffer_get (hikaru->eprom, size, offs));
		} else if (bank >= hikaru->maskrom_bank[0] &&
		           bank <= hikaru->maskrom_bank[1]) {
			/* ROMBD MASKROM */
			/* XXX probably wrong, reads wrong data in AIRTRIX;
			 * note that get_SAMURAI_params () handles address bit
			 * 25 in a special way. */
			set_ptr (val, size, vk_buffer_get (hikaru->maskrom, size, offs));
		}
		return 0;

	} else if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF) {
		/* Slave RAM */
		set_ptr (val, size, vk_buffer_get (hikaru->ram_s, size, offs));
	} else {
		VK_LOG (" addr=%X bus_addr=%X bank=%X eprom_bank=%X ", addr, bus_addr, bank, hikaru->eeprom_bank);
		VK_ASSERT (0);
	}
	return 0;
}

static int
hikaru_memctl_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_memctl_t *memctl = (hikaru_memctl_t *) dev;
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	uint32_t bank, offs, bus_addr;

	if (addr >= 0x04000000 && addr <= 0x0400003F) {
		/* MEMCTL MMIOs */
		switch (addr & 0xFF) {
		case 0x04:
			VK_ASSERT (size == 2);
			{
				uint16_t old;
				old = vk_buffer_get (memctl->regs, size, 0x04);
				val = (old & 0xF000) |
				      (old & ~val & 0xFC0) | /* Master BUS/DMA error */
				      (old & ~val & 0x03F); /* Slave BUS/DMA error */
			}
			break;
		case 0x06:
			VK_ASSERT (size == 2);
			break;
		case 0x30:
		case 0x34:
		case 0x38:
			VK_ASSERT (size == 4);
			break;
		}
		vk_buffer_put (memctl->regs, size, addr & 0x3F, val);
		return 0;
	}

	bank = get_bank_for_addr (memctl, addr);
	if (!bank)
		return -1;

	offs = addr & 0xFFFFFF;
	bus_addr = (bank << 24) | offs;

	if (bus_addr >= 0x04000000 && bus_addr <= 0x043FFFFF) {
		/* Unknown, A */
		vk_buffer_put (hikaru->unkram[0], size, offs, val);
	} else if (bus_addr >= 0x06000000 && bus_addr <= 0x063FFFFF) {
		/* Unknown, B */
		vk_buffer_put (hikaru->unkram[1], size, offs, val);
	} else if (bus_addr >= 0x0A000000 && bus_addr <= 0x0A00FFFF) {
		/* Unknown */
	} else if (bus_addr >= 0x0C000000 && bus_addr <= 0x0CFFFFFF) {
		/* AICA 1 */
	} else if (bus_addr >= 0x0D000000 && bus_addr <= 0x0DFFFFFF) {
		/* AICA 2 */
	} else if (bus_addr >= 0x0E000000 && bus_addr <= 0x0E00FFFF) {
		/* Network board */
	} else if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF) {
		/* Slave RAM */
		vk_buffer_put (hikaru->ram_s, size, offs, val);
	} else if (bank == hikaru->eeprom_bank) {
		/* ROMBD EEPROM */
	} else {
		VK_LOG (" addr=%X bus_addr=%X bank=%X eprom_bank=%X ", addr, bus_addr, bank, hikaru->eeprom_bank);
		VK_ASSERT (0);
	}
	return 0;
}

static int
hikaru_memctl_exec (vk_device_t *dev, int cycles)
{
	hikaru_memctl_t *memctl = (hikaru_memctl_t *) dev;
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	uint32_t src, dst, len, ctl;

	src = vk_buffer_get (memctl->regs, 4, 0x30);
	dst = vk_buffer_get (memctl->regs, 4, 0x34);
	len = vk_buffer_get (memctl->regs, 4, 0x38) & 0xFFFFFF;
	ctl = vk_buffer_get (memctl->regs, 4, 0x38) >> 24;

	/* DMA is running */
	if (ctl & 1) {
		vk_buffer_t *srcbuf;
		vk_buffer_t *dstbuf;
		int count;

		VK_LOG (" ### MEMCTL DMA: %08X -> %08X x %08X", src, dst, len);

		srcbuf = NULL;
		if (src >= 0x90000000 && src <= 0x9FFFFFFF)
			srcbuf = hikaru->eprom;
		else if (src >= 0xA0000000 && src <= 0xAFFFFFFF)
			srcbuf = hikaru->maskrom;

		dstbuf = NULL;
		if (dst >= 0x40000000 && dst <= 0x41FFFFFF)
			dstbuf = hikaru->ram_s;
		else if (dst >= 0x70000000 && dst <= 0x71FFFFFF)
			dstbuf = hikaru->ram_m;

		count = MIN2 ((int) len, cycles);
		len -= count;

		VK_ASSERT ((len & 0xFF000000) == 0);

		if (srcbuf && dstbuf) {
			while (count--) {
				uint32_t tmp;
				tmp = vk_buffer_get (srcbuf, 4, src & 0x0FFFFFFF);
			//	if (!(count & 0xFF))
			//		VK_LOG (" ### MEMCTL DMA: %08X --[%08X]--> %08X, still %X",
			//		        src, tmp, dst, count);
				vk_buffer_put (dstbuf, 4, dst & 0x0FFFFFFF, tmp);
				src += 4;
				dst += 4;
			}
		} else {
			src += count * 4;
			dst += count * 4;
		}

		/* Transfer completed */
		if (len == 0) {
			ctl = 0;
			/* Set DMA done, clear error flags */
			vk_buffer_put (memctl->regs, 2, 0x04, 0x1000);
			/* Raise IRL1 */
			hikaru_raise_irq (memctl->base.mach, 1, 0);
		}

		/* Write the values back */
		vk_buffer_put (memctl->regs, 4, 0x30, src);
		vk_buffer_put (memctl->regs, 4, 0x34, dst);
		vk_buffer_put (memctl->regs, 4, 0x38, (ctl << 24) | len);
	}
	return 0;
}

static void
hikaru_memctl_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_memctl_t *memctl = (hikaru_memctl_t *) dev;

	vk_buffer_clear (memctl->regs);
	vk_buffer_put (memctl->regs, 4, 0x00, memctl->master ? 0 : 0xFFFFFFFF);
}

static int
hikaru_memctl_save_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static int
hikaru_memctl_load_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static void
hikaru_memctl_delete (vk_device_t **dev_)
{
	if (dev_) {
		hikaru_memctl_t *memctl = (hikaru_memctl_t *) *dev_;
		if (memctl)
			vk_buffer_delete (&memctl->regs);
		free (memctl);
		*dev_ = NULL;
	}
}

vk_device_t *
hikaru_memctl_new (vk_machine_t *mach, bool master)
{
	hikaru_memctl_t *memctl = ALLOC (hikaru_memctl_t);
	vk_device_t *device = (vk_device_t *) memctl;
	if (!memctl)
		goto fail;

	memctl->master = master;
	memctl->regs = vk_buffer_le32_new (0x40, 0);
	if (!memctl->regs)
			goto fail;

	memctl->base.mach = mach;

	memctl->base.delete	= hikaru_memctl_delete;
	memctl->base.reset	= hikaru_memctl_reset;
	memctl->base.exec	= hikaru_memctl_exec;
	memctl->base.get	= hikaru_memctl_get;
	memctl->base.put	= hikaru_memctl_put;
	memctl->base.save_state	= hikaru_memctl_save_state;
	memctl->base.load_state	= hikaru_memctl_load_state;

	return device;

fail:
	hikaru_memctl_delete (&device);
	return NULL;
}
