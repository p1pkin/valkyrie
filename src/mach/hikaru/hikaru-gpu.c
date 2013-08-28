/* 
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
 * 
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-gpu-private.h"
#include "mach/hikaru/hikaru-renderer.h"

/* TODO: handle slave access here */

/*
 * Overview
 * ========
 *
 * Unknown hardware, possibly tailor-made for the Hikaru by SEGA. All ICs are
 * branded SEGA, and the PCI IDs are as well.  The GPU includes two distinct
 * PCI IDs: 17C7:11DB and 17C3:11DB. The former is visible from the master SH-4
 * side, the latter from the slave side.
 *
 * The GPU includes at least:
 *
 *  - A command processor (CP) which processes geometric primitives, and an
 *    image generator/rasterizer. The former is controlled by the MMIOs at
 *    15xxxxxx, the second by the MMIOs at 1Axxxxxx.
 *
 *    The CP executes instructions located in CMDRAM. It has an etherogeneous
 *    32-bit instruction set with variable-lenght instructions. It is capable
 *    of calling sub-routines, and therefore is likely to hold a stack
 *    somewhere.
 *
 *    The CMDRAM is located at 48000000-403FFFFF in external BUS space,
 *    14000000-143FFFFF in master SH-4 space, and 10000000-103FFFFF in slave
 *    SH-4 space.
 *
 *    See hikaru-gpu-cp.c for more details.
 *
 *    NOTE: there may be two command processors. Notice how there are a bunch
 *    of symmetrical graphical-related ICs on the motherboard, and many MMIOs
 *    contain mirrors that get filled with the same values plus an offset, for
 *    instance 150000{18+,38+} and 1500007{4,8}.
 *
 *  - An indirect DMA device (IDMA), which is used to load texture data into
 *    TEXRAM; each texture transferred is accompanied by metadata (size,
 *    format, etc.) The TEXRAM holds texture data used by the 3D command
 *    processor.
 *
 *    The device can be accessed through the MMIOs at 150000{0C,10,14}.
 *
 *  - A DMA device, used to move textures around in FB. In particular, it is
 *    used to transfer bitmap data directly to the 2D layers, which are then
 *    composed with the framebuffer data (the result of 3D rendering) and
 *    displayed on screen by the image generator.
 *
 *    The device can be accessed thru the MMIOs at 1A0400{00,04,08,0C}.
 *
 * There are likely two different hardware revisions: the bootrom checks for
 * them by checking the reaction of the hardware (register 15002000) after
 * poking a few registers. See @0C001AC4.
 *
 *
 * Address Space
 * =============
 *
 * The GPU has access to the whole external BUS address space. See
 * hikaru-memctl.c for more details.
 *
 *
 * MMIOs at 15000000
 * =================
 *
 * NOTE: all ports are 32-bit wide, unless otherwise noted.
 *
 * Display Config (Likely)
 * -----------------------
 *
 * 15000000   W		Unknown; = 0
 * 15000004   W		Display mode
 *			 0 = hi-res (640x480, 31 KHz)
 *			 1 = lo-res (496x384, 24 KHz)
 *			See @0C001AD8, @0C00792C.
 * 15000008   W		Unknown; = 0
 *
 * GPU IDMA
 * --------
 *
 * 150000{0C,10,14}	Indirect DMA (IDMA) MMIOs
 *
 * See 'GPU IDMA' below.
 *
 * GPU 15 Unknown Config A
 * -----------------------
 *
 * 15000018   W		Unknown; = 0       ; ~ 48000000
 * 1500001C   W		Unknown; = 00040000; ~ 48100000
 * 15000020   W		Unknown; = 00048000; ~ 48120000
 * 15000024   W		Unknown; = 00058000; ~ 48160000
 * 15000028   W		Unknown; = 00007800; ~ 4801E000
 * 1500002C   W		Unknown; = 0007FE00; ~ 481FF800
 * 15000030   W		Unknown; = 0       ; ~ 48000000
 * 15000034   W		Unknown; = 00005000; ~ 48014000
 *
 * GPU 15 Unknown Config B
 * -----------------------
 *
 * 15000038   W		Unknown; = 00080000; ~ 48200000
 * 1500003C   W		Unknown; = 000C0000; ~ 48300000
 * 15000040   W		Unknown; = 000C8000; ~ 48320000
 * 15000044   W		Unknown; = 000D8000; ~ 48360000
 * 15000048   W		Unknown; = 0000F800; ~ 4803E000
 * 1500004C   W		Unknown; = 000FFE00; ~ 483FF800
 * 15000050   W		Unknown; = 00008000; ~ 48020000
 * 15000054   W		Unknown; = 0000D000; ~ 48034000
 *
 * NOTE: same as Config A, plus an offset of +80000 or +8000.
 *
 * Command Processor Control
 * -------------------------
 *
 * 15000058   W		CP Control; = 3
 *			If both bits 0 and 1 are set, start CP execution
 * 15000070   W		CP Start Address
 * 15000074   W		CP Processor 0 Stack Pointer?
 * 15000078   W		CP Processor 1 Stack Pointer?
 * 1500007C   W		CP Abort Execution when 0-then-1 are written?
 *			 See @0C006AFC.
 *
 * Unknown
 * -------
 *
 * 15000080   W		Unknown; Control; = 6	
 *
 * Interrupt Control
 * -----------------
 *
 * 15000084   W		GPU IRQ Mask
 * 15000088  RW		GPU IRQ Status
 *			 80 = GPU 1A IRQ fired
 *			 40 = Unknown
 *			 20 = Unknown
 *			 10 = Unknown
 *			 08 = Unknown
 *			 04 = GPU 15 is ready/done; see @0C0018B4
 *			 02 = Unknown; possibly VBLANK
 *			 01 = IDMA done; see @0C006C04
 *			All bits are AND'ed on write
 *
 * Unknown
 * -------
 *
 * 1500008C   W		Unknown; = 02020202
 * 15000090   W		Unknown; = 0
 * 15000094   W		Unknown; = 0
 * 15000098   W		Unknown; = 02020202
 *			See @0C001A82
 *
 * Unknown
 * -------
 *
 * 15002000  R		Unknown; Status
 *			Used to:
 *			 - determine if the GPU is done doing FOO (together
 *			   with bit 0 of 1A000024), see @0C0069E0 and
 *			   sync() in basically all games.
 *			 - determine the HARDWARE VERSION:
 *				 - 0=older
 *				 - 1=newer
 *			   See @0C001AC4, PH:@0C01220C
 *
 * Performance Counters
 * --------------------
 *
 * These ports return the number of primitives of a certain type pushed/
 * traversed/rendered by the hardware.
 *
 * 15002800  R		# of Opaque Primitives
 * 15002804  R		# of 'Shadow A' Primitives
 * 15002808  R		# of 'Shadow B' Primitives
 * 1500280C  R		# of Transparent Primitive
 * 15002810  R		# of Background Primitives
 * 15002814  R		# of Translucent Primitives
 * 15002820  R		# of 'Set Primitive' Operations
 * 15002824  R		# of 'Render Primitive' Operations
 * 15002840  R		# of Texture Heads
 * 15002844  R		# of Materials
 * 15002848  R		# of Lights
 *
 * See PH:@0C0127B8 and the menu list at PH:@0C1D4EA4. Counter names are taken
 * from the latter.
 *
 * Unknown
 * -------
 *
 * 1502C100   W		Unknown; = 9
 * 1502C104   W		Unknown; = 6
 *
 * 15040E00   W		Unknown; = 0
 *
 *
 * MMIOs at 18001000
 * =================
 *
 * NOTE: these ports are always read twice.
 *
 * 18001000  R		PCI ID: 17C7:11DB, a SEGA ID. See @0C0019AE
 * 18001004   W		Unknown; = 2
 *
 * 18001010   W		Unknown; = F2000000
 * 18001014   W		Unknown; = F2040000
 * 18001018   W		Unknown; = F2080000
 * 1800101C   W		Unknown; = F3000000
 *
 *
 * MMIOs at 1A000000
 * =================
 *
 * NOTE: these ports are always read twice.
 *
 * Unknown
 * -------
 *
 * 1A000000   W 	GPU 1A Enable A; b0 = enable; See @0C0069E0, @0C006AFC
 * 1A000004   W 	GPU 1A Enable B; b0 = enable;
 *			See @0C0069E0, @0C006AFC, PH:@0C01276E.
 *
 * Interrupt Control
 * -----------------
 *
 * 1A000008   W		IRQ 0 Source
 * 1A00000C   W		IRQ 1 Source (GPU is done/ready)
 * 1A000010   W		IRQ 2 Source (v-blank occurred)
 * 1A000014   W		IRQ 3 Source
 *
 *			-------- -------- -------- -------i
 *
 *			i = IRQ n is raised
 *
 * 1A000018  RW		IRQ Status
 *
 *			-------- -------- -------- ----3210
 *
 *			n = IRQ n is raised
 *
 *			NOTE: when any of the IRQs is raised, bit 7 of
 *			15000088 is set.
 *
 *			NOTE: it may be related to 1A0000C4, see @0C001ED0.
 *
 * Unknown
 * -------
 *
 * 1A00001C  R		Current Raster Position
 *
 *	   		-------- -------- -----xxx xxxxxxxx
 *	   		-------- --YYYYYY YYYYY--- --------
 *	   		-------U U------- -------- --------
 *
 *	   		X = Current X position (postulated)
 *	   		Y = Current Y position
 *	   		    See PH:@0C01C106
 *	   		U = Unknown; Even/odd field status
 *	   		    - Gets stored in [0C00F070].w, (56, GBR)
 *	   		      in particular, it replaces the whole
 *	   		      bitmask (101) in sync (); See e.g.
 *	   		      @0C006A7A, PH:@0C01C158.
 *	   		    - Affects the argument to command 781
 *	   		    - Affects how much data is sent to the
 *	   		      texture-to-texture DMA in PH.
 *
 * 1A000020  R		Even/Odd Frame Status?
 *
 *	   		-------- -------- -------- -------U
 *
 *	   		U = Unknown
 *
 *	   		It is stored in [0C00F070].w, (56, GBR) in
 *	   		install_gpu_subroutine (), @0C0080E0.
 *	   		Notably, only bit 0 of the GBR var can
 *	   		possibly be set; bit 2 is always cleared.
 *
 *	   		It affects which set of GPRs is used for
 *	   		GPU upload. See @0C008130.
 *
 * 1A000024  R		Unknown Status
 *
 *	   		-------- -------- -------- -------U
 *
 *	   		U = Unknown (in vblank?)
 *
 *	   		NOTEs:
 *	   		- 15000058 bits 0,1 and GPU jump instructions, see @0C0018B4
 *	   		- 15002000 bit 0, see @?
 *	   		- HW version, see @?
 *	   		- @0C0069E8 loops while the bit is set
 *	   		- it is set on frame change
 *	   		- Also related to GPU texture upload (acts as a busy bit); see SN-ROM:@0C070C9C
 *
 * Display Config
 * --------------
 *                                           ----------------------
 *                                            640x480      496x377	AIRTRIX
 *                                           ----------------------
 * 1A000080             l  W    = 0000027F   639          818		00000332
 * 1A000084             l  W    = 000001A0   416          528		00000210
 * 1A000088             l  W    = 02680078   616 | 120    798 | 158	031E009E
 * 1A00008C             l  W    = 0196001D \ 406 |  29    516 |  36	02040024
 * 1A000090             l  W    = 02400000 | 576 |   0    728 |   0	02D80000
 * 1A000094             l  W    = 00000040 |   0 |  64      0 |  91	0000005B
 * 1A000098             l  W    = 00000003 |   0 |   3      0 |   3	00000003
 * 1A00009C             l  W    = 00000075 |   0 | 117      0 | 155	0000009B
 * 1A0000A0             l  W    = 00000198 /   0 | 408      0 | 574	0000023E
 * 1A0000A4             l  W    = 001D0194 \  29 | 404     36 | 514	00240202
 * 1A0000A8             l  W    = 00000195 |   0 | 405      0 | 515	00000203
 * 1A0000AC             l  W    = 00000000 |   0 |   0      0 |   0	00000000
 * 1A0000B0             l  W    = 00000000 |   0 |   0      0 |   0	00000000
 * 1A0000B4             l  W    = 00000000 |   0 |   0      0 |   0	00000000
 * 1A0000B8             l  W    = 00000179 /   0 | 377      0 | 416	000001A0
 * 1A0000BC             l  W    = 00000008     0 |   8      0 |   8	00000008
 * 1A0000C0             l  W    = 01960000   406 |   0      0 | 516	02040000
 *
 * Unknown
 * -------
 *
 * 1A0000C4   W		Unknown control
 *
 *			-------- -------- -------- -----uu-
 *
 *			u = Unknown
 *
 * 1A0000D0   W		Unknown control
 *
 *			-------- -------- -------- -------u
 *
 *			u = Unknown
 *
 * Framebuffer/2D Layer Control
 * ----------------------------
 *
 * 1A000100  RW		Unknown control
 *
 *			-------- -------- -------- ----uuuu
 *
 *			u = Unknown; a bitfield
 *
 *			See @0C007D00, PH:@0C01A0F8, PH@0C01A10C.
 *
 *			NOTE: perhaps it selects between FB unit 0 and unit 1
 *			for on a per-layer basis?
 *
 * 1A000180-1A0001BF	FB Unit 0 MMIOs (four banks)
 * 1A000200-1A00023F	FB Unit 1 MMIOs (four banks)
 *
 * Unit 0 and 1 MMIOs are identical. The values stored in unit 0 MMIOs are
 * copied into unit 1 MMIOs in @0C00689E. Each bank supposedly specifies a
 * layer or a framebuffer(/backbuffer?) location in the FB. No idea why there
 * are two units though.
 *
 * See '2D Layers' below.
 *
 * Unknown
 * -------
 *
 * 1A020000   W		"SEGA" is written here; see @0C001A58
 *
 * GPU DMA
 * -------
 *
 * 1A04000{0,4,8,C}	DMA MMIOs
 *
 * See 'GPU DMA' below.
 *
 * Unknown
 * -------
 *
 * 1A08006C  R		Unknown, See AT:@0C697A5E.
 *
 * 1A0A1600   W		Unknown (seems related 15040E00, see PHARRIER)
 */

/* GPU IRQs
 * ========
 *
 * IRQs are signalled to the main SH-4 through IRL2: priority 7, INTEVT 0x300,
 * vector 0x220. At the same time, bit 4 of the master SH-4 Port A is cleared
 * (it is active low).
 *
 * The two registers 15000088 and 1A000018 signal 8 and 4 different IRQs
 * causes, respectively. When any of the IRQs in 1A000018 is raised, bit 7 of
 * 15000088 is set. 15000084 is the IRQ mask register, and is applied to
 * 15000088.
 *
 * Bits in 15000088:
 *
 *  01, bit 0 = GPU 15 indirect DMA done
 *  02, bit 1 = Unknown but required for the bootrom to work
 *  04, bit 2 = GPU 15 done / ready
 *  08, bit 3 = Unknown
 *  10, bit 4 = Unknown
 *  20, bit 5 = Unknown
 *  40, bit 6 = Unknown
 *  80, bit 7 = 1A0000xx IRQ mirror
 *
 * Bits in 1A000088:
 *
 *  01, bit 0 = Unknown
 *  02, bit 1 = Vblank-out or Hblank-out [1]
 *  04, bit 2 = GPU 1A done / ready
 *  08, bit 4 = Unknown
 *
 * IRQs at 1A000018 are most likely related to Texture/FB operations, that is,
 * anything related to the 1A00xxxx registers (including texture FIFO, etc.)
 * When raised, they set bit 8 in 15000088.
 *
 * [1] This bit is checked at @0C001C08 and updates (0, GBR) and implies
 *     1A000000 = 1.
 */

static void
hikaru_gpu_update_irq_status (hikaru_gpu_t *gpu) 
{
	vk_cpu_t *cpu = ((hikaru_t *) gpu->base.mach)->sh_current;

	/* Update 1A000018 from 1A0000[08,0C,10,14] */
	REG1A (0x18) = (REG1A (0x18) & ~0xF) |
	               (REG1A (0x08) & 1) |
	               ((REG1A (0x0C) & 1) << 1) |
	               ((REG1A (0x10) & 1) << 2) |
	               ((REG1A (0x14) & 1) << 3);

	/* Update 15000088 bit 7 from 1A000018 */
	if (REG1A (0x18) & 0xF)
		REG15 (0x88) |= 0x80;

	/* Raise IRL2 and lower bit 5 of the PDTRA, if the IRQs are
	 * not masked. */
	if (REG15 (0x88) & REG15 (0x84)) {
		VK_CPU_LOG (cpu, " ## sending GPU IRQ to CPU: %02X/%02X",
		            REG15 (0x84), REG15 (0x88));
		hikaru_raise_gpu_irq (gpu->base.mach);
	}
}

void
hikaru_gpu_raise_irq (hikaru_gpu_t *gpu, uint32_t _15, uint32_t _1A)
{
	if (_1A & 1)
		REG1A (0x08) |= 1;
	if (_1A & 2)
		REG1A (0x0C) |= 1;
	if (_1A & 4)
		REG1A (0x10) |= 1;
	if (_1A & 8)
		REG1A (0x14) |= 1;
	REG15 (0x88) |= _15;
	hikaru_gpu_update_irq_status (gpu);
}

/*
 * Framebuffer and Texture RAM
 * ===========================
 *
 * The framebuffer RAM (FB) is located at 1B000000-1B7FFFFF in the master
 * SH-4 address space. It acts as a single 2048x2048 sheet. It holds the
 * framebuffer and the 2D layers, plus tile data. It is accessible both
 * directly by the CPU or indirectly through the GPU DMA device. It is used
 * for 2D only.
 *
 * Framebuffer data can hold both ABGR1555? and ABGR8888 data.
 *
 * The two texture RAM (TEXRAM) areas are located at 04000000-043FFFFF
 * (bank 0) and 06000000-063FFFFF (bank 1) of the external address space,
 * respectively. Each bank acts as a 2048x1024 sheet. The two banks hold
 * texture data and are directly accessible by the GPU. They are mainly
 * accessed by the CPU through the GPU IDMA device. They are used for 3D
 * only.
 *
 * Textures can be ABGR1555, ABGR4444, ABGR1111, and A8?; other formats
 * may be possible. Textures may or may not include a complete mipmap tree.
 *
 *
 *
 * Framebuffer Configuration
 * =========================
 *
 * The GPU is able to specify four regions of FB to be used as either
 * framebuffer (front and back, likely) or 2D bitmap layers. The mapping
 * looks like this:
 *
 * Bank 0 = Framebuffer?
 * Bank 1 = Backbuffer?
 * Bank 2 = Layer 1
 * Bank 3 = Layer 2
 *
 * The number of bitmap layers here conforms to the system16.com specs.
 *
 * The device is configured through the following MMIOs:
 *
 * 1A000180,4		Unit 0, Bank 0 Coords
 * 1A000188,C		Unit 0, Bank 1 Coords
 * 1A000190,4		Unit 0, Bank 2 Coords
 * 1A000198,C		Unit 0, Bank 3 Coords
 *
 *			-------- -----yyy yyyyyyyx xxxxxxxx
 *			-------- -----YYY YYYYYYYX XXXXXXXX
 *
 *			x,y = Coordinates of upper right corner
 *			X,Y = Coordinates of lower left corner
 *
 *			The actual scale of x and X is determined by the layer
 *			format.
 *
 *			See PH:@0C01A1A6, PH:@0C01A860.
 *
 * 1A0001B0  RW		FB/Layer Format
 *
 *			-------- -------- -------- -----21F
 *
 *			F = framebuffer/backbuffer format
 *			1 = layer 1 format
 *			2 = layer 2 format
 *
 *			If the format is 0, then the layer is ABGR8888;
 *			otherwise it is ABGR1555?
 *
 *			See PH:@0C01A1EA.
 *
 * 1A0001B4  RW		Layer Enable
 *
 *			-------- -------- -------- ------21
 *
 *			n = enable layer n
 *
 *			See PH:@0C01A124, PH:@0C01A142.
 *
 * 1A0001B8  RW		Unknown Control
 *
 *			-------- -------- -------- ------??
 *
 *			Bitfield or not? See PH:@0C01A184, PH:@0C01A18A.
 *
 * 1A0001BC  RW		Unknown Control
 *
 *			-------- -------- -------- -----???
 *
 *			Bitfield. See PH:@0C01A162, PH:@0C01A171.
 *
 * The MMIOs at 1A000200-1A00023F have the very same layout. Sometimes they
 * are written with the contents of the unit 0 MMIOs. Their purpose is still
 * unknown.
 *
 * NOTE: it may be the case that unit 1 is used for a second GPU/screen.
 */

static void
hikaru_gpu_fill_layer_info (hikaru_gpu_t *gpu)
{
	uint32_t unit, bank;

	VK_LOG ("==== BEGIN LAYERS ==== [1A:100=%X]", REG1A (0x100));
	gpu->layers.enabled = 1; // REG1A (0x100) & 1;

	for (unit = 0; unit < 2; unit++) {

		VK_LOG ("\tLAYER UNIT %u: fmt=%08X ena=%08X unk1=%08X unk2=%08X",
		        unit,
		        REG1AUNIT (unit, 0x30), REG1AUNIT (unit, 0x34),
		        REG1AUNIT (unit, 0x38), REG1AUNIT (unit, 0x3C));

		for (bank = 2; bank < 4; bank++) {
			uint32_t bank_offs = bank * 8, lo, hi, format, shift;
			hikaru_gpu_layer_t *layer =
				&gpu->layers.layer[unit][bank-2];
	
			/* Is the layer enabled? */
			layer->enabled = REG1AUNIT (unit, 0x34) >> (bank - 2);
	
			/* Get the layer format. */
			format = (REG1AUNIT (unit, 0x30) >> (bank - 1)) & 1;
			if (format == 0) {
				layer->format = HIKARU_FORMAT_ABGR1555;
				shift = 1;
			} else {
				layer->format = HIKARU_FORMAT_ABGR8888;
				shift = 2;
			}
	
			/* Get layer coords in the FB. */
			lo = REG1AUNIT (unit, bank_offs + 0);
			hi = REG1AUNIT (unit, bank_offs + 4);
	
			layer->x0 = (lo & 0x1FF) << shift;
			layer->y0 = lo >> 9;
			layer->x1 = (hi & 0x1FF) << shift;
			layer->y1 = hi >> 9;

			if (layer->enabled)
				VK_LOG ("\tLAYER %u:%u : %s",
				        unit, bank, get_gpu_layer_str (layer));
		}
	}
}

/*
 * GPU Indirect DMA (IDMA)
 * =======================
 *
 * This device is used to transfer texture data from the BUS to the TEXRAM.
 * It is controlled by the MMIOs at 150000{0C,10,14} as follows:
 *
 * 1500000C   W		Indirect DMA address
 *
 *			aaaaaaaa aaaaaaaa aaaaaaaa aaaaaaaa
 *
 *			a = BUS address of the IDMA table
 *
 * 15000010  RW		Indirect DMA lenght
 *
 *			-------- -------- nnnnnnnn nnnnnnnn
 *
 *			n = Number of entries to process
 *
 * 15000014  RW		Indirect DMA Control
 *
 *			-------- -------- -------- -------E
 *
 *			E = Process the DMA request when set; signal busy
 *			    or ready when read
 *
 * 1500001C points to a table in CMDRAM, defaulting to 483FC000. Each entry
 * is 4 words long, and has the following format:
 *
 *         MSB                               LSB
 *	+00 AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *	+04 -------- -------- SSSSSSSS SSSSSSSS
 *	+08 ---fff-- --hhhwww yyyyyyyy xxxxxxxx
 *	+0C                            -------b
 *
 * 	A = a BUS address; it specifies where the texels are stored. Valid
 *          locations seen so far are the CMDRAM (48xxxxxx) and slave RAM
 *          (41xxxxxx).
 *
 *	S = texture size in bytes; it includes the size of the whole
 *          mipmap tree (if present.) That is, the size for a 64x64 texture
 *	    is:
 *
 *	    (64*64+32*32+16*16+8*8+4*4+2*2+1)*2 = 0x2AAA bytes
 *
 *	    Without a mipmap it would just be: 64*64*2 = 0x2000 bytes.
 *
 *	y = Destination slot y
 *
 *	x = Destination slot x
 *
 *	    Each slot is 16x16 pixels; the origin (i.e., the lowest
 *	    addressable slot coords) seems to be (80, C0). The slot determines
 *	    the TEXRAM coords within the selected bank (see below.)
 *
 *	w = log_16 of width
 *
 *	h = log_16 of height 
 *
 *		0 = 16
 *		1 = 32
 *		2 = 64
 *		3 = 128
 *		4 = 256
 *		5,6,7 = Unused?
 *
 *	f = Format:
 *
 *		0 = ABGR1555
 *		1 = ABGR4444
 *		2 = ABGR1111
 *		4 = A8?
 *
 *	b = Destination TEXRAM bank
 *
 * The IDMA fires GPU 15 IRQ 1 when done.
 *
 * NOTE: the upper half of +08 likely uses the same format as instruction
 * 2C1; the lower half likely uses the same format as 4C1. If this is the
 * case, then bits 22-26 and 29-31 may not be unused --- in fact, there's
 * evidence that they are used within the 2C1 instruction.
 *
 * NOTE: AIRTRIX uploads textures as blocks of 512x512 pixels. The odd thing
 * is that format=0 while not all textures in the block are ABGR1555.
 *
 * NOTE: IDMA it may be related to vblank timing, see PH:@0C0128E6 and
 * PH:@0C01290A.
 */

static uint32_t
calc_full_texture_size (hikaru_gpu_texhead_t *texhead)
{
	uint32_t w = texhead->width;
	uint32_t h = texhead->height;
	uint32_t size = 0;

	while (w > 0 && h > 0) {
		size += w * h;
		w = w / 2;
		h = h / 2;
	}
	return size * 2;
}

static void
copy_texture (hikaru_gpu_t *gpu, uint32_t bus_addr, hikaru_gpu_texhead_t *texhead)
{
	hikaru_t *hikaru = (hikaru_t *) gpu->base.mach;
	uint32_t basex, basey, endx, endy;
	uint32_t mask, x, y, offs, bank;
	vk_buffer_t *srcbuf;

	if ((bus_addr >> 24) == 0x48) {
		srcbuf = hikaru->cmdram;
		mask = 8*MB-1;
	} else {
		srcbuf = hikaru->ram_s;
		mask = 32*MB-1;
	}

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty);

	endx = basex + texhead->width;
	endy = basey + texhead->height;

	if (gpu->options.log_idma) {
		VK_LOG ("GPU IDMA: %ux%u to (%X,%X), area in TEXRAM is ([%u,%u],[%u,%u]); dst addr = %08X",
		        texhead->width, texhead->height,
		        texhead->slotx, texhead->sloty,
		        basex, basey, endx, endy,
		        basey * 4096 + basex * 2);
	}

	if ((endx > 2048) || (endy > 1024)) {
		VK_ERROR ("GPU IDMA: out-of-bounds transfer: %s",
		          get_gpu_texhead_str (texhead));
		return;
	}

	offs = bus_addr & mask;
	bank = texhead->bank;
	for (y = 0; y < texhead->height; y++) {
		for (x = 0; x < texhead->width; x++, offs += 2) {
			uint32_t temp = (basey + y) * 4096 + (basex + x) * 2;
			uint32_t texel = vk_buffer_get (srcbuf, 2, offs);
			vk_buffer_put (gpu->texram[bank], 2, temp, texel);
		}
	}
}

static void
process_idma_entry (hikaru_gpu_t *gpu, uint32_t entry[4])
{
	hikaru_gpu_texhead_t texhead;
	uint32_t exp_size[2], bus_addr, size;

	memset (&texhead, 0, sizeof (texhead));

	bus_addr = entry[0];
	size     = entry[1];

	texhead.slotx	= entry[2] & 0xFF;
	texhead.sloty	= (entry[2] >> 8) & 0xFF;
	texhead.width	= 16 << ((entry[2] >> 16) & 7);
	texhead.height	= 16 << ((entry[2] >> 19) & 7);
	texhead.format	= (entry[2] >> 26) & 7;
	texhead.bank	= entry[3] & 1;

	/* Compute the expected size in bytes */
	exp_size[0] = texhead.width * texhead.height * 2;
	exp_size[1] = calc_full_texture_size (&texhead);

	/* XXX this check isn't clever enough to figure out that AIRTRIX
	 * texture blobs _do_ have a mipmap. However even if we miss a
	 * mipmap or two, it shouldn't be that big of a problem. */
	texhead.has_mipmap = (size == exp_size[1]) ? 1 : 0;

	if (gpu->options.log_idma) {
		VK_LOG ("GPU IDMA %08X %08X %08X %08X : %s",
		        entry[0], entry[1], entry[2], entry[3],
		        get_gpu_texhead_str (&texhead));
	}

	if ((entry[2] & 0xE3C00000) ||
	    (entry[3] & 0xFFFFFFFE)) {
		VK_ERROR ("GPU IDMA: unhandled bits in texture entry: %08X %08X %08X %08X",
		          entry[0], entry[1], entry[2], entry[3]);
		/* continue anyway */
	}

	if (size != exp_size[0] && size != exp_size[1]) {
		VK_ERROR ("GPU IDMA: unexpected texhead size: %s",
		          get_gpu_texhead_str (&texhead));
		/* continue anyway */
	}

	if (texhead.format != HIKARU_FORMAT_ABGR1555 &&
	    texhead.format != HIKARU_FORMAT_ABGR4444 &&
	    texhead.format != HIKARU_FORMAT_ABGR1111 &&
	    texhead.format != HIKARU_FORMAT_ALPHA8) {
		VK_ERROR ("GPU IDMA: unknown texhead format: %s",
		          get_gpu_texhead_str (&texhead));
		/* continue anyway */
	}

	if (texhead.slotx < 0x80 || texhead.sloty < 0xC0) {
		VK_ERROR ("GPU IDMA: unknown texhead slot, skipping: %s",
		          get_gpu_texhead_str (&texhead));
		return;
	}

	if ((bus_addr & 0xFE000000) != 0x40000000 &&
	    (bus_addr & 0xFF000000) != 0x48000000) {
		VK_ERROR ("GPU IDMA: unknown texhead address, skipping: %s",
		          get_gpu_texhead_str (&texhead));
		return;
	}

	copy_texture (gpu, bus_addr, &texhead);
}

static void
hikaru_gpu_step_idma (hikaru_gpu_t *gpu)
{
	uint32_t entry[4], addr;

	/* XXX note that the bootrom code assumes that the IDMA may stop even
	 * if there are still unprocessed entries. This probably means that
	 * the IDMA may stop processing when any other GPU IRQ fires. There's
	 * no solid proof however, and it doesn't seem to be required. */

	if (!(REG15 (0x14) & 1) || !REG15 (0x10))
		return;

	VK_ASSERT ((REG15 (0x0C) >> 24) == 0x48);

	/* Read the IDMA table address in CMDRAM */
	addr = (REG15 (0x0C) & 0xFFFFFF);

	entry[0] = vk_buffer_get (gpu->cmdram, 4, addr+0x0);
	entry[1] = vk_buffer_get (gpu->cmdram, 4, addr+0x4);
	entry[2] = vk_buffer_get (gpu->cmdram, 4, addr+0x8);
	entry[3] = vk_buffer_get (gpu->cmdram, 4, addr+0xC);

	/* If the entry supplies a positive size, process it */
	if (entry[1]) {
		process_idma_entry (gpu, entry);
		REG15 (0x0C) += 0x10;
		REG15 (0x10) --;
	}

	/* If there are no more entries, stop */
	if (REG15 (0x10) == 0) {
		REG15 (0x14) = 0;
		hikaru_gpu_raise_irq (gpu, _15_IRQ_IDMA, 0);
	}
}

/*
 * GPU DMA
 * =======
 *
 * Copies texture data around within the FB. It's used to compose the intro
 * text of AIRTRIX and PHARRIER, for instance. It is controlled by the ports
 * at 1A0000{0,4,8,C} as follows:
 *
 * 1A040000   W		Source coords
 *
 *			-------- --yyyyyy yyyyyxxx xxxxxxxx 
 *
 *			x,y = Coordinates in pixels
 *
 * 1A040004   W		Destination coords
 *
 *			-------- --yyyyyy yyyyyxxx xxxxxxxx 
 *
 *			x,y = Coordinates in pixels
 *
 * 1A040008   W		Size
 *
 *			hhhhhhhh hhhhhhhh wwwwwwww wwwwwwww
 *
 *			w = Width in pixels
 *			h = height in pixels
 *
 * 1A04000C   W		Control
 *
 *			-------- -------- -------- -------E
 *
 *			E = Process the DMA request when set
 *
 * See AT:@0C697D48, PH:@0C0CD320.
 *
 * NOTE: 1A000024 bit 0 seems to signal when the device is busy.
 *
 */

/* XXX convert to begin/step/end; according to PHARRIER, the DMA operation
 * should take more or less C cycles for each texel, where C is a small
 * constant. */

static void
hikaru_gpu_begin_dma (hikaru_gpu_t *gpu)
{
	uint32_t *fifo = (uint32_t *) gpu->regs_1A_fifo;
	uint32_t src_x, src_y, dst_x, dst_y, w, h, i, j;

	src_x = fifo[0] & 0x7FF;
	src_y = fifo[0] >> 11;

	dst_x = fifo[1] & 0x7FF;
	dst_y = fifo[1] >> 11;

	w = fifo[2] & 0xFFFF;
	h = fifo[2] >> 16;

	if (gpu->options.log_dma) {
		VK_LOG ("GPU DMA: [%08X %08X %08X %08X] { %u %u } --> { %u %u }, %ux%u",
		        fifo[0], fifo[1], fifo[2], fifo[3],
			src_x, src_y, dst_x, dst_y, w, h);
	}

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			uint32_t src_offs = (src_y + i) * 4096 + (src_x + j) * 2;
			uint32_t dst_offs = (dst_y + i) * 4096 + (dst_x + j) * 2;
			uint16_t pixel;
			pixel = vk_buffer_get (gpu->fb, 2, src_offs);
			vk_buffer_put (gpu->fb, 2, dst_offs, pixel);
		}
	}

	REG1A (0x24) |= 1;
}

/****************************************************************************
 External events
****************************************************************************/

void
hikaru_gpu_hblank_in (vk_device_t *dev, unsigned line)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	/* Update the line counter; we ignore the _putative_ pixel counter
	 * as it doesn't seem to be used so far. */
	REG1A(0x1C) = (REG1A(0x1C) & ~0x003FF800) | (line << 11);
}

void
hikaru_gpu_vblank_in (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	hikaru_gpu_cp_vblank_in (gpu);
}

void
hikaru_gpu_vblank_out (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	hikaru_gpu_raise_irq (gpu, _15_IRQ_VBLANK, _1A_IRQ_VBLANK);
	hikaru_gpu_fill_layer_info (gpu);
	hikaru_gpu_cp_vblank_out (gpu);
}

static int
hikaru_gpu_exec (vk_device_t *dev, int cycles)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	/* Exec the DMA */
	/* XXX */

	/* Exec the IDMA */
	hikaru_gpu_step_idma (gpu);

	/* Exec the CP */
	if (REG15 (0x58) == 3)
		hikaru_gpu_cp_exec (gpu, cycles);

	return 0;
}

/****************************************************************************
 Accessors
****************************************************************************/

static int
hikaru_gpu_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	uint16_t *val16 = (uint16_t *) val;
	uint32_t *val32 = (uint32_t *) val;

	VK_ASSERT (size == 4 ||
	           (size == 2 && (addr == 0x15000010 || addr == 0x00400000)));

	*val32 = 0;
	if (addr == 0x00400000) {
		set_ptr (val, size, gpu->unk_00400000);
	} else if (addr >= 0x15000000 && addr < 0x15000100) {
		switch (addr & 0xFF) {
		case 0x10:
			if (size == 2) {
				set_ptr (val, 2, REG15 (addr));
				return 0;
			}
		case 0x14:
		case 0x88:
			break;
		default:
			return -1;
		}
		*val32 = REG15 (addr);
	} else if (addr == 0x15002000) {
		/* no-op */
	} else if (addr == 0x18001000) {
		/* SEGA PCI ID */
		*val32 = 0x17C711DB;
	} else if (addr >= 0x1A000000 && addr < 0x1A000140) {
		switch (addr & 0x1FF) {
		case 0x18: /* GPU 1A IRQ Status */
			*val32 = (REG1A (0x08) & 1) |
			         ((REG1A (0x0C) & 1) << 1) |
			         ((REG1A (0x10) & 1) << 2) |
			         ((REG1A (0x14) & 1) << 3);
			break;
		case 0x1C:
		case 0x20: /* XXX ^= 1 */
		case 0x24: /* XXX = 2 */
		case 0x100:
			break;
		default:
			return -1;
		}
		*val32 = REG1A (addr);
	} else if (addr >= 0x1A000180 && addr < 0x1A0001C0) {
		*val32 = REG1AUNIT (0, addr);
	} else if (addr >= 0x1A000200 && addr < 0x1A000240) {
		*val32 = REG1AUNIT (1, addr);
	} else if (addr >= 0x1A080000 && addr < 0x1A080100) {
		/* ??? */
	}
	return 0;
}

static int
hikaru_gpu_put (vk_device_t *device, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) device;

	VK_ASSERT (size == 4 ||
	           (size == 2 && addr == 0x00400000));

	if (addr == 0x00400000) {
		gpu->unk_00400000 = val;
	} else if (addr >= 0x15000000 && addr < 0x15000100) {
		switch (addr & 0xFF) {
		case 0x00:
		case 0x04:
		case 0x08:
		case 0x0C:
		case 0x10:
		case 0x14:
		case 0x18 ... 0x34:
		case 0x38 ... 0x54:
		case 0x70 ... 0x78:
		case 0x80:
		case 0x8C:
		case 0x90:
		case 0x94:
		case 0x98:
			break;
		case 0x58: /* CP Control */
			REG15 (0x58) = val;
			hikaru_gpu_cp_on_put (gpu);
			return 0;
		case 0x84: /* IRQ mask */
			REG15 (addr) = val;
			hikaru_gpu_update_irq_status (gpu);
			return 0;
		case 0x88: /* IRQ status */
			REG15 (addr) &= val;
			hikaru_gpu_update_irq_status (gpu);
			return 0;
		default:
			return -1;
		}
		REG15 (addr) = val;
	} else if (addr == 0x1502C100) {
		VK_ASSERT (val == 9);
	} else if (addr == 0x1502C104) {
		VK_ASSERT (val == 6);
	} else if (addr == 0x15040E00) {
		VK_ASSERT (val == 0);
	} else if (addr >= 0x18001000 && addr < 0x18001020) {
		REG18 (addr) = val;
	} else if (addr >= 0x1A000000 && addr < 0x1A000104) {
		switch (addr & 0x1FF) {
		case 0x00:
		case 0x04:
			/* XXX update FIFO/layer status? */
			break;
		case 0x08:
		case 0x0C:
		case 0x10:
		case 0x14:
			/* Bit 0 is ANDNOT'ed on write; I have no clue about
			 * the other bits. */
			VK_ASSERT (val == 1);
			REG1A(addr) &= ~val;
			hikaru_gpu_update_irq_status (gpu);
			return 0;
		case 0x24:
			REG1A (addr) = val;
			hikaru_gpu_cp_on_put (gpu);
			return 0;
		case 0x80 ... 0xC0: /* Display Config? */
		case 0xC4: /* Unknown control */
		case 0xD0: /* Unknown control */
		case 0x100:
			break;
		default:
			return -1;
		}
		REG1A (addr) = val;
	} else if (addr >= 0x1A000180 && addr < 0x1A0001C0) {
		REG1AUNIT (0, addr) = val;
	} else if (addr >= 0x1A000200 && addr < 0x1A000240) {
		REG1AUNIT (1, addr) = val;
	} else if (addr == 0x1A020000) {
		/* "SEGA" */
		VK_ASSERT (val == 0x53454741);
	} else if (addr >= 0x1A040000 && addr < 0x1A040010) {
		REG1AFIFO (addr) = val;
		if (addr == 0x1A04000C && (val & 1) == 1)
			hikaru_gpu_begin_dma (gpu);
	} else if (addr == 0x1A0A1600) {
		VK_ASSERT (val == 1);
	}
	return 0;
}

/****************************************************************************
 Interface
****************************************************************************/

static void
hikaru_gpu_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	memset (gpu->regs_15, 0, 0x100);
	memset (gpu->regs_18, 0, 0x100);
	memset (gpu->regs_1A, 0, 0x104);
	memset (gpu->regs_1A_unit[0], 0, 0x40);
	memset (gpu->regs_1A_unit[1], 0, 0x40);
	memset (gpu->regs_1A_fifo, 0, 0x10);

	memset (&gpu->viewports, 0, sizeof (gpu->viewports));
	memset (&gpu->materials, 0, sizeof (gpu->materials));
	memset (&gpu->texheads, 0, sizeof (gpu->texheads));
	memset (&gpu->lights, 0, sizeof (gpu->lights));

	gpu->cp.is_running = 0;
}

const char *
hikaru_gpu_get_debug_str (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	static char out[256];

	sprintf (out, "@%08X %u 15:58=%u 1A:24=%u 15:84=%X 15:88=%X 1A:18=%X",
	         gpu->cp.pc, (unsigned) gpu->cp.is_running,
	         REG15 (0x58), REG1A (0x24),
	         REG15 (0x84), REG15 (0x88), REG1A (0x18));

	return (const char *) out;
}

static int
hikaru_gpu_save_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static int
hikaru_gpu_load_state (vk_device_t *dev, FILE *fp)
{
	return -1;
}

static void
hikaru_gpu_destroy (vk_device_t **dev_)
{
	if (dev_) {
		hikaru_gpu_t *gpu = (hikaru_gpu_t *) *dev_;
		free (gpu);
		*dev_ = NULL;
	}
}

vk_device_t *
hikaru_gpu_new (vk_machine_t *mach,
                vk_buffer_t *cmdram,
                vk_buffer_t *fb,
                vk_buffer_t *texram[2],
                vk_renderer_t *renderer)
{
	hikaru_gpu_t *gpu = ALLOC (hikaru_gpu_t);
	vk_device_t *dev = (vk_device_t *) gpu;

	if (!gpu)
		return NULL;

	dev->mach = mach;

	dev->destroy	= hikaru_gpu_destroy;
	dev->reset	= hikaru_gpu_reset;
	dev->exec	= hikaru_gpu_exec;
	dev->get	= hikaru_gpu_get;
	dev->put	= hikaru_gpu_put;
	dev->save_state	= hikaru_gpu_save_state;
	dev->load_state	= hikaru_gpu_load_state;

	gpu->cmdram	= cmdram;
	gpu->fb		= fb;
	gpu->texram[0]	= texram[0];
	gpu->texram[1]	= texram[1];
	gpu->renderer	= renderer;

	gpu->options.log_dma =
		vk_util_get_bool_option ("GPU_LOG_DMA", false);
	gpu->options.log_idma =
		vk_util_get_bool_option ("GPU_LOG_IDMA", false);
	gpu->options.log_cp =
		vk_util_get_bool_option ("GPU_LOG_CP", false);

	hikaru_gpu_cp_init (gpu);

	VK_STATIC_ASSERT (sizeof (hikaru_gpu_texhead_t) == (sizeof (uint32_t) * 4));

	return dev;
}
