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

/* TODO: handle the memctl apertures with manipulating the hikaru's mmaps
 * directly. */

/*
 * Memory Controller
 * =================
 *
 * The Hikaru mainboard is connected to a number of external boards: the
 * ROM board (ROMBD), the primary sound board, an optional sound board, an
 * optional network board.
 *
 * Access to these devices is performed through an 'external BUS'. The
 * memory controller decides which external device is mapped to what
 * part of the SH-4 address space.
 *
 * There are (very likely) two of these memory controllers, one for each
 * SH-4 CPU. They are likely the two xxx-xxxxx SEGA ICs conveniently placed
 * in the proximity of the two CPUs.
 *
 * Aside from memory device mapping, they also provide a DMA facility to
 * transfer data directly from different devices on the external BUS.
 *
 * Mapping
 * =======
 *
 * Direct access to these extenral devices is possible by mapping them in
 * the SH-4 with the memory controller: in particular, they get mapped within
 * a couple of apertures: one is located at 02000000-03FFFFFF, the other
 * at 16000000-17FFFFFF. Other portions of the SH-4 address space (possibly
 * all of it) may be managed in a similar way, but apparently their emulation
 * is not critical (the mapping don't seem to change.)
 *
 * What is mapped to the apertures is determined by the  MMIOs. See
 * get_bank_for_addr () for the details. Each byte in the MMIOs (+10, +14,
 * and +18 are confirmed) controls a 16 MB aperture. See @0C006688.
 *

 * MMIO Ports
 * ==========
 *
 *  Offset	+3       +2       +1       +0
 *  +0x00	IIIIIIII IIIIIIII IIIIIIII IIIIIIII	Controller ID
 *  +0x04	---u---- -------- ---sEEEE EEFFFFFF	DMA Status
 *  +0x08	-------- -------- -------- --------
 *  +0x0C	-------- -------- -------- --------
 *  +0x10	dddddddd cccccccc bbbbbbbb aaaaaaaa	Aperture 0 Address
 *  +0x14	hhhhhhhh gggggggg ffffffff eeeeeeee	Aperture 1 Address
 *  +0x18	llllllll kkkkkkkk jjjjjjjj iiiiiiii	Aperture 2 Address
 *  +0x1C	pppppppp oooooooo nnnnnnnn mmmmmmmm	Aperture 0 Control
 *  +0x20	tttttttt ssssssss rrrrrrrr qqqqqqqq	Aperture 1 Control
 *  +0x24	xxxxxxxx wwwwwwww vvvvvvvv uuuuuuuu	Aperture 2 Control
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
 * Note: other interesting evidence is at PH:@0C0124B8.
 *
 * Note: accessing 3C may alter other registers; for instance, the code at
 * @0C001748  saves/restores 04000018 before accessing 0400003C.
 *
 * Note: accessing the bus (apertures) may give rise to errors, both during
 * DMA operation and during normal access; these errors get reported in
 * fields E and F.
 *
 *
 * DMA Operation
 * =============
 *
 * The DMA is likely used to transfer data from the main RAM to devices on
 * different boards (ROMBD, SNDBD, SNDBD2, NETBD). DMA operation is initiated
 * by setting bit 24 of +38. Upon termination, the MEMCTL raises IRL 1 on
 * the master SH-4, and sets bit 12 of +04 and its corresponding error field.
 *
 * See @0C008640 for more details XXX
 *
 * Note: it may be the case that bit 12 and the error field are mutually
 * exclusive.
 *
 *
 * External BUS Address Space
 * ==========================
 *
 * The address space of the external BUS is as follows:
 *
 * 04000000-043FFFFF	Unknown; GPU-related Area (Frame Buffer?)
 * 06000000-063FFFFF	Unknown; GPU-related Area (Frame Buffer?)
 * 0A000000-0A00FFFF	Unknown; ROMBD-related
 * 0C000000-0CFFFFFF	Sound Board 1
 * 0D000000-0DFFFFFF	Sound Board 2 [Optional]
 * 0E000000-0E00FFFF	Network Board [Optional]
 * 10000000-3FFFFFFF	ROMBD (EPROM, MASKROM, EEPROM get mapped here)
 * 40000000-41FFFFFF	Slave RAM
 * 48000000-483FFFFF	GPU CMDRAM
 * 4C000000-4C3FFFFF	GPU Unknown
 * 70000000-71FFFFFF	Master RAM
 *
 * Note: apparently the external bus is 31 bits wide or less. The MSB is
 * used in mysterious ways. For instance, the code uses the MEMCTL DMA to
 * read ROM data, but accesses the following ranges:
 *
 * 90000000-9FFFFFFF - 80000000 = 10000000-1FFFFFFF	EPROM
 * A0000000-AFFFFFFF - 80000000 = 20000000-2FFFFFFF	MASKROM
 *
 *
 * Rom Board (ROMBD)
 * =================
 *
 * There are two known types of ROM board (which are neatly documented in
 * the MAME hikaru driver.) The EPROM/MASKROM external BUS address can be
 * recovered from information at offset +13C of the EPROM data.
 *
 * This is what this data looks like:
 *
 *	AIRTRIX (Type 1)		ICs	Size	
 *	================		===	====	
 *	
 *	0003 fee8 c889 97c2 620c	29,30	2 x 4MB	= 8, OK
 *	ffff 0000 0000 0000 0000	 		
 *	ffff 0000 0000 0000 0000	 		
 *	ffff 0000 0000 0000 0000	 		
 *	0005 b9a5 9e67 a52a bce0	37,38	2 x 16MB = 32, OK
 *	ffff 0000 0000 0000 0000	 
 *	0005 dabb b621 4bd4 5e6b	41,42	2 x 16MB = 32, OK
 *	ffff 0000 0000 0000 0000	 
 *	0005 0d06 ad63 790f a27e	45,46	2 x 16MB = 32, OK
 *	ffff 0000 0000 0000 0000	 
 *	0005 bdbb 4f01 14a7 6a4e	49,50	2 x 16MB = 32, OK
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	
 *	BRAVEFF (Type ?)		ICs	Size	
 *	================		===	====	
 *	
 *	0002 0000 0000 0000 0000 	29,30	2 x 2MB = 4, OK
 *	0002 be43 7023 7077 f161	31,32	2 x 2MB = 4, OK
 *	0002 c60d d4f0 b533 8f66	33,34	2 x 2MB = 4, OK
 *	ffff 0000 0000 0000 0000 
 *	0004 8613 2876 3700 2f6d 		2 x 4MB = 8, OK
 *	0004 f545 a454 b97e bb4c 
 *	0004 d6ff 6fe3 df40 f343 
 *	0004 e3b6 2f23 b2b6 61c7 
 *	0004 4792 7015 853b faf1 
 *	0004 c44b 7a18 24dc e336 
 *	0004 e3b0 e492 17bb e589 
 *	0004 cd9f 08f3 3183 cd5c 
 *	0004 8fb6 3fa8 ebbb 9ed9 
 *	0004 2316 c644 66cc b590 
 *	0004 47d0 320d e677 85ad 
 *	0004 d76d cf62 4d9e 8564 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	
 *	PHARRIER			ICs	Size	
 *	========			===	====	
 *	
 *	0003 0000 0000 0000 0000	29,30	2 x 4MB 
 *	0003 388c 13b5 d289 5910	31,32	2 x 4MB
 *	0003 09a8 578f 73e5 94f7	33,34	2 x 4MB
 *	0003 b1de 6dad 9019 6dd5	35,36	2 x 4MB
 *	0005 7f16 2c37 1f9f aae5	37,38	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 986c 8d7a bd1d 5304	41,42	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 9784 b33d cb75 b08b	45,46	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 5056 b3a9 cbde be85	49,50	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 3d36 05bf d629 8ed6	53,54	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 b9f5 0082 5875 8163	57,58	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 b19d e7cc 158c d180	61,62	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	0005 34bc 677b 5524 349e	65,66	2 x 16MB
 *	ffff 0000 0000 0000 0000	
 *	
 *	PODRACE (Type 2)		ICs	Size	
 *	================		===	====	
 *	
 *	0003 0000 0000 0000 0000	29,30	2 x 4MB
 *	0003 5f01 4174 3594 38b3	31,32	2 x 4MB
 *	ffff 0000 0000 0000 0000	
 *	ffff 0000 0000 0000 0000	
 *	0004 7993 8e18 4d44 d239	37,38	2 x 16MB
 *	0004 4135 beab f0c8 04e2	39,40	2 x 16MB
 *	0004 9532 4c1c 925d 02fb	41,42	2 x 16MB
 *	0004 0809 7050 72bc 9311	43,44	2 x 16MB
 *	0004 de84 9d8a 7a5c e7fc	45,46	2 x 16MB
 *	0004 6806 1392 edf1 7bd1	47,48	2 x 16MB
 *	0004 b82d e114 5792 e5e5	49,50	2 x 16MB
 *	0004 3af3 a97c a8cc 721d	51,52	2 x 16MB
 *	0004 ced7 d3cf 6b67 fc76	53,54	2 x 16MB
 *	0004 586c 6954 13a0 db38	55,56	2 x 16MB
 *	0004 4f03 42bf 8ea6 adb6	57,58	2 x 16MB
 *	0004 8645 fc30 3847 ca6b	59,60	2 x 16MB
 *	0004 4140 01c4 ebe6 8085	61,62	2 x 16MB
 *	0004 b68b 7467 4715 4787	63,64	2 x 16MB
 *	0004 3cd6 144a e5d3 ba35	65,66	2 x 16MB
 *	0004 e668 08ed 1fe8 c4a1	67,68	2 x 16MB
 *	
 *	SGNASCAR (Type 2)		ICs	Size	
 *	=================		===	====	
 *	
 *	0003 0000 0000 0000 0000	35,36	2 x 4MB
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	0005 0352 d263 49fd 4ad3	19,20	2 x 16MB 
 *	0005 e717 d635 3637 0e8e	21,22
 *	0005 4001 8dab c65d bde3	23,24
 *	0005 615c 293d 7507 1d85	25,26
 *	0005 90a2 eccc 2b1e 2f9b	27,28
 *	0005 c98b 3ffb 51e3 701b	29,30
 *	0005 523f 2979 953c 2e5c	31,32
 *	0005 28cf 283f f17b 74fb	33,34	2 x 16MB 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *	ffff 0000 0000 0000 0000 
 *
 * The mapping from IC numbers to entries has been determined by matching
 * the CRCs in the entries and the CRC values reported in the MAME driver.
 *
 * Each entry (10 bytes) can be defined like this:
 *
 *	struct {
 *		uint16_t log_size;
 *		uint16_t bytesum_lo;
 *		uint16_t wordsum_lo;
 *		uint16_t bytesum_hi;
 *		uint16_t wordsum_hi;
 *	} rom_layout[4+16];
 *
 * and describes the size and CRC for an IC pair: the first four entries
 * describe the EPROMs, the remaining 16 the MASKROMs. If the first word
 * is FFFF, those ICs are not populated.
 *
 * Given the i'th entry, the size and location of an IC (pair) can be
 * computed as follows (see @0C004ED2):
 *
 *	base = samurai_tag_at (0x18000000) ? 8 :
 *	       samurai_tag_at (0x10000000) ? 0 : 0;
 *	
 *	size = 1*MB << rom_layout[i].log_size
 *	
 *	if (i < 4)
 *		///////////////////////////////////////////
 *		// EPROM
 *		// SAMURAI at 10000000 -> base = 0 -> EPROM at 10,11,12,13
 *		// SAMURAI at 18000000 -> base = 8 -> EPROM at 18,19,1A,1B
 *		// each section is 4 or 8 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base + 0x10 + i
 *
 *	else if ((_0C00F01C & C) != 8)
 *		///////////////////////////////////////////
 *		// MASKROM, type 1
 *		// SAMURAI at 10000000 -> base = 0 -> MASKROM at 20,21,22,23,...
 *		// SAMURAI at 18000000 -> base = 8 -> MASKROM at 30,31,32,33,...
 *		// each section is 16 or 32 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base * 2 + 0x1C + i 
 *
 *	else
 *		///////////////////////////////////////////
 *		// MASKROM, type 2
 *		// SAMURAI at 10000000 -> base = 0 -> MASKROM at 20,22,24,26,...
 *		// SAMURAI at 18000000 -> base = 8 -> MASKROM at 30,32,34,36,...
 *		// each section is 16 or 32 MB (2 ICs)
 *		///////////////////////////////////////////
 *		bank = base * 2 + 0x20 + (i - 4) * 2
 *	
 *	bus_addr = bank >> 24
 *	if (size < 8*MB)
 *		bus_addr += 4*MB
 *
 * Here base is determined by the bootrom (see @0C00B834) by looking for
 * the 'SAMURAI' string at both BUS addresses 18000000 and 10000000. If
 * the string is found at bank 18, then the base is 8; it's 0 otherwise.
 *
 * What IC is mapped to what entry is determined by bits 2 and 3 of 0C00F01C.
 * See memctl_get () and @0C004B32. 
 */

typedef struct {
	vk_device_t base;

	vk_buffer_t *regs;
	bool master;
} hikaru_memctl_t;

/* TODO modify the hikaru->mmap_[ms] instead */

/* TODO raise m/s bus error on bad access */

static int
rombd_get (hikaru_t *hikaru, unsigned size, uint32_t bus_addr, void *val)
{
	hikaru_rombd_config_t *config = &hikaru->rombd_config;
	uint32_t bank = bus_addr >> 24;
	uint32_t offs = bus_addr & 0xFFFFFF;
	uint32_t real_offs;
	bool log = false;

	/* Access here is valid even if performed on the wrong banks: we set
	 * the ptr to garbage here because the hikaru bootrom reads
	 * indiscriminately from banks 10-1B (including the EPROM bank!) to
	 * figure out the EPROM format. We don't want spurious matchings
	 * (that is, 0 vs. 0) to affect the computation. */
	set_ptr (val, size, rand ());

	/* Nothing else to do if there's no actual ROM data */
	if (!config->has_rom)
		return 0;

	if (bank >= config->eprom_bank[0] &&
	    bank <= config->eprom_bank[1]) {
		/* ROMBD EPROM */
		uint32_t num = bank - config->eprom_bank[0]; /* 0 ... 3 */
		uint32_t bank_size = config->eprom_bank_size == 2 ? 4*MB : 8*MB;
		uint32_t bank_mask = bank_size - 1;

		real_offs = (offs & bank_mask) + num * bank_size;
		if (real_offs < vk_buffer_get_size (hikaru->eprom))
			set_ptr (val, size, vk_buffer_get (hikaru->eprom, size, real_offs));
		else
			log = true;

	} else if (bank >= config->maskrom_bank[0] &&
	           bank <= config->maskrom_bank[1]) {
		/* ROMBD MASKROM */
		/* XXX take in account MASKROM stretching here */
		uint32_t num = bank - config->maskrom_bank[0]; /* 0 ... 15 */
		uint32_t bank_size = config->maskrom_bank_size == 8 ? 16*MB : 32*MB;
		uint32_t bank_mask = bank_size - 1;

		real_offs = (offs & bank_mask) + num * bank_size;
		if (real_offs < vk_buffer_get_size (hikaru->maskrom))
			set_ptr (val, size, vk_buffer_get (hikaru->maskrom, size, real_offs));
		else
			log = true;
	}
	if (log)
		VK_CPU_LOG (hikaru->sh_current, "ROMBD R%u %08X [%08X]", size * 8, bus_addr, real_offs);
	return 0;
}

static int
memctl_bus_get (hikaru_memctl_t *memctl, unsigned size, uint32_t bus_addr, void *val)
{
	hikaru_t *hikaru = (hikaru_t *) memctl->base.mach;
	uint32_t bank = bus_addr >> 24;
	uint32_t offs = bus_addr & 0xFFFFFF;
	bool log = false;

	set_ptr (val, size, 0);
	if (bus_addr >= 0x04000000 && bus_addr <= 0x043FFFFF) {
		/* Unknown A */
		set_ptr (val, size, vk_buffer_get (hikaru->unkram[0], size, offs));
	} else if (bus_addr >= 0x06000000 && bus_addr <= 0x063FFFFF) {
		/* Unknown B */
		set_ptr (val, size, vk_buffer_get (hikaru->unkram[1], size, offs));
	} else if (bus_addr >= 0x0A000000 && bus_addr <= 0x0AFFFFFF) {
		/* Unknown */
		/* Here's the thing: the value of bits 2 and 3 of 0C00F01C
		 * (which is GBR 28) depends on whether these two ports
		 * retain the value '0x19620217'.
		 * 
		 * If the value of the upper two bits is 4, then the EPROM
		 * start at IC 29; they start at 35 otherwise. See
		 * @0C004BF8.
		 *
		 * If the value is 8, then the MASKROM placement in the bus
		 * address space is non-linear. See @0C004F82 for details.
		 *
		 * Judging by the ROM file extensions, we want these bits
		 * to be 4 for everything except SGNASCAR, and 8 for
		 * SGNASCAR (?). PHARRIER should be '4' type, but the ROM
		 * zip contains two IC35's, one EPROM and one MASKROM.
		 */
		switch (offs) {
		case 0x8:
			if (!hikaru->rombd_config.maskrom_is_stretched)
				set_ptr (val, size, 0x19620217);
			break;
		case 0xC:
			if (hikaru->rombd_config.maskrom_is_stretched)
				set_ptr (val, size, 0x19620217);
			break;
		}
		VK_CPU_LOG (hikaru->sh_current, "ROMBD CTL R%u %08X", size * 8, bus_addr);
	} else if (bus_addr >= 0x0C000000 && bus_addr <= 0x0CFFFFFF) {
		/* AICA 1 */
		return vk_device_get (hikaru->aica_m, size, bus_addr, val);
	} else if (bus_addr >= 0x0D000000 && bus_addr <= 0x0DFFFFFF) {
		/* AICA 2 */
		return vk_device_get (hikaru->aica_s, size, bus_addr, val);
	} else if (bus_addr >= 0x0E000000 && bus_addr <= 0x0E00FFFF) {
		/* Network Board */
		log = true;
	} else if (bus_addr >= 0x10000000 && bus_addr <= 0x3FFFFFFF) {
		/* ROMBD */
		return rombd_get (hikaru, size, bus_addr, val);
	} else if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF) {
		/* Slave RAM */
		set_ptr (val, size, vk_buffer_get (hikaru->ram_s, size, bus_addr & 0x01FFFFFF));
	} else if (bus_addr >= 0x70000000 && bus_addr <= 0x71FFFFFF) {
		/* Master RAM */
		set_ptr (val, size, vk_buffer_get (hikaru->ram_m, size, bus_addr & 0x01FFFFFF));
	} else if (bank == hikaru->rombd_config.eeprom_bank && offs == 0) {
		/* ROMBD EEPROM */
		set_ptr (val, size, 0xFFFFFFFF);
	} else
		return -1;
	if (log)
		VK_CPU_LOG (hikaru->sh_current, "MEMCTL R%u %08X", size * 8, bus_addr);
	return 0;
}

static int
memctl_bus_put (hikaru_memctl_t *memctl, unsigned size, uint32_t bus_addr, uint64_t val)
{
	hikaru_t *hikaru = (hikaru_t *) memctl->base.mach;
	uint32_t bank = bus_addr >> 24;
	uint32_t offs = bus_addr & 0xFFFFFF;
	bool log = false;

	if (bus_addr >= 0x04000000 && bus_addr <= 0x043FFFFF) {
		/* Unknown, A */
		vk_buffer_put (hikaru->unkram[0], size, offs, val);
	} else if (bus_addr >= 0x06000000 && bus_addr <= 0x063FFFFF) {
		/* Unknown, B */
		vk_buffer_put (hikaru->unkram[1], size, offs, val);
	} else if (bus_addr >= 0x0A000000 && bus_addr <= 0x0AFFFFFF) {
		/* Unknown */
		log = true;
	} else if (bus_addr >= 0x0C000000 && bus_addr <= 0x0CFFFFFF) {
		/* AICA 1 */
		return vk_device_put (hikaru->aica_m, size, bus_addr, val);
	} else if (bus_addr >= 0x0D000000 && bus_addr <= 0x0DFFFFFF) {
		/* AICA 2 */
		return vk_device_put (hikaru->aica_s, size, bus_addr, val);
	} else if (bus_addr >= 0x0E000000 && bus_addr <= 0x0E00FFFF) {
		/* Network board */
		log = true;
	} else if (bus_addr >= 0x40000000 && bus_addr <= 0x41FFFFFF) {
		/* Slave RAM */
		vk_buffer_put (hikaru->ram_s, size, bus_addr & 0x01FFFFFF, val);
	} else if (bus_addr >= 0x70000000 && bus_addr <= 0x71FFFFFF) {
		/* Master RAM */
		vk_buffer_put (hikaru->ram_m, size, bus_addr & 0x01FFFFFF, val);
	} else if (bank == hikaru->rombd_config.eeprom_bank && offs == 0) {
		/* ROMBD EEPROM */
	} else
		return -1;
	if (log)
		VK_CPU_LOG (hikaru->sh_current, "MEMCTL W%u %08X = %X", size * 8, bus_addr, val);
	return 0;
}

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

static int
hikaru_memctl_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_memctl_t *memctl = (hikaru_memctl_t *) dev;
	uint32_t bank;

	if (addr >= 0x04000000 && addr <= 0x0400003F) {
		/* MMIOs */
		set_ptr (val, size, vk_buffer_get (memctl->regs, size, addr & 0x3F));
		return 0;
	}

	bank = get_bank_for_addr (memctl, addr) & 0x7F;
	if (!bank)
		return -1;

	return memctl_bus_get (memctl, size, (bank << 24) | (addr & 0xFFFFFF), val);
}

static int
hikaru_memctl_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_memctl_t *memctl = (hikaru_memctl_t *) dev;
	uint32_t bank;

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

	bank = get_bank_for_addr (memctl, addr) & 0x7F;
	if (!bank)
		return -1;

	return memctl_bus_put (memctl, size,
	                       (bank << 24) | (addr & 0xFFFFFF), val);
}

static int
hikaru_memctl_exec (vk_device_t *dev, int cycles)
{
	hikaru_memctl_t *memctl = (hikaru_memctl_t *) dev;
	uint32_t src, dst, len, ctl;

	src = vk_buffer_get (memctl->regs, 4, 0x30);
	dst = vk_buffer_get (memctl->regs, 4, 0x34);
	len = vk_buffer_get (memctl->regs, 4, 0x38) & 0xFFFFFF;
	ctl = vk_buffer_get (memctl->regs, 4, 0x38) >> 24;

	/* DMA is running */
	if (ctl & 1) {
		int count;

		VK_LOG (" ### MEMCTL DMA: %08X -> %08X x %08X", src, dst, len);

		count = MIN2 ((int) len, cycles);
		len -= count;

		VK_ASSERT ((len & 0xFF000000) == 0);

		while (count--) {
			uint32_t tmp;
			memctl_bus_get (memctl, 4, src & 0x7FFFFFFF, &tmp);
			memctl_bus_put (memctl, 4, dst & 0x7FFFFFFF, tmp);
			src += 4;
			dst += 4;
		}

		/* Transfer completed */
		if (len == 0) {
			ctl = 0;
			/* Set DMA done, clear error flags */
			vk_buffer_put (memctl->regs, 2, 0x04, 0x1000);
			/* Raise IRL1 */
			hikaru_raise_irq (memctl->base.mach, 1, 0);
			VK_LOG (" ### MEMCTL DMA DONE!");
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
