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

#include <string.h>

#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-gpu-private.h"
#include "mach/hikaru/hikaru-renderer.h"
#include "cpu/sh/sh4.h"

/* TODO: figure out what is 4Cxxxxxx; there are a few CALL commands to that
 * bus bank in the BOOTROM command streams. */
/* TODO: handle slave access */
/* TODO: figure out the following: 
 * Shading		Flat, Linear, Phong
 * Lighting		Horizontal, Spot, 1024 lights per scene, 4 lights
 *			per polygon, 8 window surfaces. 
 * Effects		Phong Shading, Fog, Depth Queueing, Stencil, Shadow [?]
 *			Motion blur
 */

/*
 * Overview
 * ========
 *
 * Unknown hardware, possibly tailor-made for the Hikaru by SEGA. All ICs are
 * branded SEGA, and the PCI IDs are as well. It is known to handle fire
 * and water effects quite well, but it's unlikely to be equipped with more
 * than a fixed-function pipeline (it was developed in 1998-1999 after all.)
 *
 * The GPU includes two distinct PCI IDs: 17C7:11DB and 17C3:11DB. The former
 * is visible from the master SH-4 side, the latter from the slave side.
 *
 * There are likely two different hardware revisions: the bootrom checks for
 * them by checking the reaction of the hardware (register 15002000) after
 * poking a few registers. See @0C001AC4.
 *
 * The GPU(s) include at least:
 *
 *  - A geometry engine (likely the command processor here; located at
 *    150xxxxx) and a rendering engine (likely the texture-related device
 *    at 1A0xxxxx).
 *
 *  - A command stream processor, which executes instructions in CMDRAM,
 *    with an etherogeneous 32-bit ISA and variable-length instructions.
 *    It is capable to call sub-routines, and so is likely to hold a
 *    stack somewhere.
 *
 *    The code
 *    is held in CMDRAM, which is at 14300000-143FFFFF in the master SH-4
 *    address space, and 48000000-483FFFFF in bus address space.
 *
 *    The device can be controlled by the MMIOs at 150000{58,...,8C}.
 *
 *    My guess is that even and odd frames are processed by two different,
 *    identical processors. NOTE that there are a bunch of 'double'
 *    symmetrical ICs on the motherboard, in the GPU-related portion.
 *
 *  - An indirect DMA-like device which is likely used to move texture
 *    data to/from TEXRAM, and is able to decode between texture formats on
 *    the fly.
 *
 *    The device can be accessed thru the MMIOs at 150000{0C,10,14}.
 *
 *  - A FIFO-like device, used to move textures around in TEXRAM. In
 *    particular, it is used to transfer bitmap data directly to the
 *    framebuffer(s).
 *
 *    The device can be accessed thru the MMIOs at 1A0400{00,04,08,0C}.
 */

/*
 * GPU Address Space
 * =================
 *
 * The GPU has access to the whole external BUS address space. See
 * hikaru-memctl.c for more details.
 *
 *
 * GPU MMIOs at 15000000
 * =====================
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
 * BUS-to-TEXRAM IDMA
 * ------------------
 *
 * 1500000C   W		Indirect DMA table address (in CMDRAM)
 * 15000010  RW		Indirect DMA # of entries to process (16 bits wide)
 * 15000014  RW		Indirect DMA Control
 *			 Bit 0: exec when set, busy when read
 *
 * See 'Texture Binder Device' below.
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
 * NOTE: 000FFE00 is so so similar to 120FEE00 ~ 483FB800 used in the BOOTROM
 *
 * XXX log the above in AIRTRIX and PHARRIER.
 *
 * Command Stream Control
 * ----------------------
 *
 * 15000058   W		CS Control; = 3
 *			If both bits 0 and 1 are set, start CS execution
 *
 * 15000070   W		CS Address; = 48000100
 * 15000074   W		CS Processor 0 SP?; = 483F0100
 * 15000078   W		CS Processor 1 SP?; = 483F8100
 * 1500007C   W		CS Abort Execution when 0-then-1 are written?
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
 *			 0x80 = GPU 1A IRQ fired
 *			 0x40 = Unknown
 *			 0x20 = Unknown
 *			 0x10 = Unknown
 *			 0x08 = Unknown
 *			 0x04 = GPU 15 is ready/done; see @0C0018B4
 *			 0x02 = Unknown; possibly VBLANK
 *			 0x01 = IDMA done; see @0C006C04
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
 * GPU MMIOs at 18001000
 * =====================
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
 * GPU MMIOs at 1A000000
 * =====================
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
 * 1A000008   W		IRQ 1A Source 0
 * 1A00000C   W		IRQ 1A Source 1; GPU 1A done
 * 1A000010   W		IRQ 1A Source 2; VBlank
 * 1A000014   W		IRQ 1A Source 3
 *
 * 1A000018  RW		IRQ 1A Status
 *			 Four bits; bit n indicates the status of the
 *			 IRQ governed by register 1A000008+(n*4)
 *
 * NOTE: when any of these bits is set, bit 7 of 15000088 is set.
 * NOTE: it may be related to 1A0000C4, see @0C001ED0.
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
 *	   		X = Current X position
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
 * NOTE: my gutter feeling is that these register specify operations that must
 * be performed at the rasterization stage to the whole contents of the frame
 * buffer.
 *
 * Unknown
 * -------
 *
 * 1A0000C4             l  W    = 6		Unknown control
 * 1A0000D0		l  W	= 1		Unknown control
 *
 * Texture RAM Control
 * -------------------
 *
 * XXX update this section; it's stale.
 *
 * 1A000100             l RW    Enable scanout (the framebuffer is displayed
 *				on-screen.)
 *				See @0C007D00 ,PH:@0C01A0F8, PH@0C01A10C,
 *
 * Framebuffer/2D Layer Control
 * ----------------------------
 *
 * XXX update this section; it's stale.
 *
 * 1A000180-1A0001BF    l RW    Framebuffer A, 16 registers
 * 1A000200-1A00023F    l RW    Framebuffer B, 16 registers
 *
 *     The UNIT's come in pairs: 9 LSBs + other, see PH:@0C01A860.
 *
 *     +0x34 lower 2 bits (at least) turn on/off a unit. See PH:@0C01A124.
 *     It uses the same (R4 < 2) check as PH:@0C01A860.
 *
 *      180             l RW    = 0x00000 \ UNIT 0                          \
 *      184             l RW    = 0x3BF3F /                                 | TEXRAM
 *      188             l RW    = 0x40000 \ UNIT 1                          | addresses in
 *      18C             l RW    = 0x7BF3F /                                 | 8-byte units
 *      190             l RW    = 0x00140 \ UNIT 2 [Reserved? See pharrier] |
 *      194             l RW    = 0x3BFDF /                                 | lower 9 bits only
 *      198             l RW    = 0x40140 \ UNIT 3 [Reserved? See pharrier] | see PH:@0C01A860
 *      19C             l RW    = 0x7BFDF /                                 /
 *      1A0             l RW    = 0 \ UNIT 0 CONTROL
 *      1A4             l RW    = 0 /
 *      1A8             l RW    = 0 \ UNIT 1 CONTROL
 *      1AC             l RW    = 0 /
 *      1B0             l RW    = 1 \ UNIT 2 CONTROL // a bitfield: see @0C007D60
 *      1B4             l RW    = 1 /
 *      1B8             l RW    = 3 \ UNIT 3 CONTROL
 *      1BC             l RW    = 6,0 /              // 6 to turn on, 0 to turn off; bitfield
 *
 *	These could be the TEXRAM content setup.
 *
 *      200-21C         are identical to 180-19C + 0x80000 [GPU CMD RAM OFFSET DIFF BETWEEN ODD/EVEN FRAMES!]
 *      220-23C         are identical to 1A0-1BC
 *
 *      The fact that there are four of these guys may be related to the
 *      fact that there are four different IRQ causes in 1A000018 -- not likely
 *
 *      Point is that the +180 regs seem to be used in even frames, +200 regs in
 *      odd ones. 200+ is written to 180+ sometimes.
 *
 * Unknown
 * -------
 *
 * 1A020000 32-bit  W	"SEGA" is written here; see @0C001A58
 *
 * TEXRAM-to-TEXRAM DMA
 * --------------------
 *
 * 1A040000 32-bit  W	Source
 * 1A040004 32-bit  W	Destination
 * 1A040008 32-bit  W	Texture Size
 * 1A04000C 32-bit  W	Control
 *
 * See 'TEXRAM-to-TEXRAM DMA Device' below.
 *
 * Unknown
 * -------
 *
 * 1A08006C 32-bit R	Unknown
 *
 * 1A0A1600 32-bit  W	Unknown (seems related 15040E00, see PHARRIER)
 */

#define REG15(addr_)	(*(uint32_t *) &gpu->regs_15[(addr_) & 0xFF])
#define REG18(addr_)	(*(uint32_t *) &gpu->regs_18[(addr_) & 0xFF])
#define REG1A(addr_)	(*(uint32_t *) &gpu->regs_1A[(addr_) & 0x1FF])
#define REG1AUNIT(n,a)	(*(uint32_t *) &gpu->regs_1A_unit[n][(a) & 0x3F])
#define REG1AFIFO(a)	(*(uint32_t *) &gpu->regs_1A_fifo[(a) & 0xF])

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
 * [1] This bit is checked in 0C001C08 and updates (0, GBR) and implies
 *     1A000000 = 1.
 */

typedef enum {
	_15_IRQ_IDMA	= (1 << 0),
	_15_IRQ_VBLANK	= (1 << 1),
	_15_IRQ_DONE	= (1 << 2),
	_15_IRQ_UNK3	= (1 << 3),
	_15_IRQ_UNK4	= (1 << 4),
	_15_IRQ_UNK5	= (1 << 5),
	_15_IRQ_UNK6	= (1 << 6),
	_15_IRQ_1A	= (1 << 7)
} _15_irq_t;

typedef enum {
	_1A_IRQ_UNK0	= (1 << 0),
	_1A_IRQ_VBLANK	= (1 << 1),
	_1A_IRQ_DONE	= (1 << 2),
	_1A_IRQ_UNK3	= (1 << 3)
} _1a_irq_t;

static void
hikaru_gpu_update_irqs (hikaru_gpu_t *gpu) 
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

	/* XXX move this to hikaru.c */

	/* Raise IRL2 and lower bit 5 of the PDTRA, if the IRQs are
	 * not masked. */
	if (REG15 (0x88) & REG15 (0x84)) {
		VK_CPU_LOG (cpu, " ## sending GPU IRQ to CPU: %02X/%02X",
		            REG15 (0x84), REG15 (0x88));
		hikaru_raise_irq (gpu->base.mach, SH4_IESOURCE_IRL2, 0x40);
	}
}

static void
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
	hikaru_gpu_update_irqs (gpu);
}

/*
 * GPU Command Processor
 * =====================
 *
 * TODO (basically opcodes are stored in CMDRAM, execution begins on X and
 * ends on Y; frequency unknown)
 *
 * GPU Instructions
 * ================
 *
 * Each GPU instruction is made of 1, 2, 4, or 8 32-bit words. The opcode is
 * specified by the lower 12 bits of the first word.
 *
 * In general, opcodes of the form:
 *
 * - xx1 seem to set properties of the current object
 * - xx2 seem to be control-flow related.
 * - xx3 seem to be used to recall a given object/offset
 * - xx4 seem to be used to commit the current object
 * - xx6 seem to be ?
 * - xx8 seem to be texture related maybe?
 *
 * GPU Execution
 * =============
 *
 * Very few things are known. Here are my guesses:
 *
 * GPU execution is likely initiated by:
 *  15000058 = 3
 *  1A000024 = 1
 *
 * It may be latched until the next vblank-in even, but I'm not sure. Modern
 * video cards don't work that way. The 781 command may serve exactly this
 * purpose though.
 *
 * A new frame subroutine is uploaded when both IRQs 2 of GPU 15 and GPU 1A
 * are fired, meaning that they both consumed the data passed in and require
 * new data (subroutine) to continue processing.
 *
 * When execution ends:
 * XXX CHECK ALL THIS AGAIN PLEASE
 *  1A00000C bit 0 is set
 *  1A000018 bit 1 is set as a consequence
 *  15000088 bit 7 is set as a consequence
 *  15000088 bit 1 is set; a GPU IRQ is raised (if not masked by 15000084)
 *  15002000 bit 0 is set on some HW revisions
 *  1A000024 bit 0 is cleared if some additional condition occurred
 *
 * 15002000 and 1A000024 signal different things; see the termination
 * condition in sync()
 */

static void
hikaru_gpu_update_cs_status (hikaru_gpu_t *gpu)
{
	/* Check the GPU 15 execute bits */
	if (REG15 (0x58) == 3) {
		gpu->cs.is_running = true;

		gpu->cs.pc = REG15 (0x70);
		gpu->cs.sp[0] = REG15 (0x74);
		gpu->cs.sp[1] = REG15 (0x78);
	} else
		REG1A (0x24) = 0; /* XXX really? */
}

static void
hikaru_gpu_end_processing (hikaru_gpu_t *gpu)
{
	/* Turn off the busy bits */
	REG15 (0x58) &= ~3;
	REG1A (0x24) &= ~1;

	/* Notify that GPU 15 is done and needs feeding */
	hikaru_gpu_raise_irq (gpu, _15_IRQ_DONE, _1A_IRQ_DONE);
}

/*
 * Texture RAM
 * ===========
 *
 * Located at 1B000000-1B7FFFFF in the master SH-4 address space, 8MB large;
 * it acts as a single 2048x2048-texels wide sheet. Texels/pixels are 16-bit
 * only, so: 2048x2048x2 bytes = 8MB. Each row is 4096 bytes wide.
 *
 * Curiously, PH:@0C01A242 uses a pitch of (1 << 11) = 8192; this may be
 * due to the way texture coordinates are encoded though.
 *
 *
 * Texture Data
 * ============
 *
 * Supported texture formats include RGBA5551, RGBA4444, pure-ALPHA and
 * others. See hikaru_gpu_texture_t for details.
 *
 * Textures may include a complete mipmap tree.
 *
 *
 * Texture DMA
 * ===========
 *
 * Copies texture data from TEXRAM to the framebuffer(s). It's used to
 * compose the intro text of AIRTRIX and PHARRIER, for instance. It is
 * controlled by the ports at 1A0000{0,4,8,C} as follows:
 *
 *	1A040000  32-bit  W	Source
 *	1A040004  32-bit  W	Destination
 *	1A040008  32-bit  W	Texture width/height in pixels
 *	1A04000C  32-bit  W	Control
 *
 * Both source and destination are encoded as TEXRAM coordinates; both
 * x and y are defined as 11-bit integers (range is 0 ... 2047); pixel
 * size is 16-bit, fixed.
 *
 * 1A000024 bit 0 seems to signal when the device is busy.
 *
 * It's not yet clear if data gets written directly to the framebuffer
 * or to a separate TEXRAM region used solely for 2D layers.
 *
 * See AT:@0C697D48, PH:@0C0CD320.
 *
 *
 * Texture Indirect DMA
 * ====================
 *
 * This device is used to either (a) register texture data into the GPU (that
 * is, to notify the GPU that some texture with a given format is stored
 * at the given location; IOW to attach metadata to a given texture), or
 * (b) perform texture upload/conversion to TEXRAM.
 *
 * This device is controlled by registers: 150000{0C,10,14}. Register
 * 1500000C points to a table in CMDRAM, defaulting to 483FC000. Each entry
 * is 4 words long, and has this format:
 *
 *         MSB                               LSB
 *	+00 AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *	+04 -------- -------- SSSSSSSS SSSSSSSS
 *	+08 ---fff-- --hhhwww yyyyyyyy xxxxxxxx
 *	+0C                            -------u
 *
 * 	A = a bus address; it specifies where the texels are stored. Valid
 *          locations seen so far are the CMDRAM (48xxxxxx) and slave RAM
 *          (41xxxxxx). Perhaps textures in TEXRAM don't require validation?
 *
 *	S = texture size in bytes; it includes the size of the whole
 *          mipmap tree. That is, the size for a 64x64 texture is:
 *
 *	    (64*64+32*32+16*16+8*8+4*4+2*2+1)*2 = 0x2AAA bytes
 *
 *	y = Unknown, possibly positional information
 *
 *	x = Unknown, possibly positional information
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
 *		0 = RGBA5551
 *		1 = RGBA4444
 *		2 = RGBA1? XXX check the BOOTROM conversion code!
 *		4 = A8?
 *
 *	u = XXX Unknown (issue IRQ when done?)
 *
 * NOTE: the upper half of +08 likely uses the same format as instruction
 * 2C1; the lower half likely uses the same format as 4C1. If this is the
 * case, then bits 22-26 and 29-31 may not be unused --- in fact, there's
 * evidence that they are used within the 2C1 instruction.
 *
 * NOTE: since the smallest texture size is 16x16, I wouldn't be surprised
 * if the x and y fields were multiples of 16 (or 16*2 if they are in
 * bytes.) However 2048 / 256 = 8, so that's perhap the proper unit.
 * However, some experiments (see idma.py and idmas.log) seem to indicate
 * that the proper unit size is in fact 8. XXX implement it! XXX
 *
 * NOTE: AIRTRIX uploads textures as blocks of 512x512 pixels. The odd thing
 * is that format=0 while not all textures in the block are RGBA5551.
 *
 * NOTE: during the bootrom life-cycle, the data address to texture-like
 * data (the not-yet-converted ASCII texture.) However, the bootrom uploads
 * the very same texture independently to TEXRAM by performing the format
 * conversion manually (RGBA1 to RGBA4); it does however use the GPU IDMA
 * mechanism too. I don't know why.
 *
 * The IDMA fires GPU 15 IRQ 1 when done.
 *
 * NOTE: it may be related to vblank timing, see PH:@0C0128E6 and
 * PH:@0C01290A.
 */

static uint32_t
calc_full_texture_size (hikaru_gpu_texture_t *texture)
{
	uint32_t w = texture->width;
	uint32_t h = texture->height;
	uint32_t size = 0;

	while (w > 0 && h > 0) {
		size += w * h;
		w = w / 2;
		h = h / 2;
	}
	return size * 2;
}

static void
copy_texture (hikaru_gpu_t *gpu, hikaru_gpu_texture_t *texture)
{
	hikaru_t *hikaru = (hikaru_t *) gpu->base.mach;
	vk_buffer_t *srcbuf;
	uint32_t basex = (texture->slotx - 0x80) * 16;
	uint32_t basey = (texture->sloty - 0x80) * 16;
	uint32_t endx = basex + texture->width;
	uint32_t endy = basey + texture->height;
	uint32_t mask, x, y, offs;

	if ((texture->addr >> 24) == 0x48) {
		srcbuf = hikaru->cmdram;
		mask = 8*MB-1;
	} else {
		srcbuf = hikaru->ram_s;
		mask = 32*MB-1;
	}

	VK_LOG ("IDMA: %ux%u to (%X,%X), area in texram is ([%u,%u],[%u,%u]); dst addr = %08X",
	        texture->width, texture->height,
	        texture->slotx, texture->sloty,
	        basex, basey, endx, endy,
	        basey * 4096 + basex * 2);

	if ((endx > 2048) || (endy > 2048)) {
		VK_ERROR ("IDMA: out-of-bounds transfer: %s",
		          get_gpu_texture_str (texture));
		return;
	}

	offs = texture->addr & mask;
	for (y = 0; y < texture->height; y++) {
		for (x = 0; x < texture->width; x++, offs += 2) {
			uint32_t temp = (basey + y) * 4096 + (basex + x) * 2;
			uint32_t texel = vk_buffer_get (srcbuf, 2, offs);
			vk_buffer_put (gpu->texram, 2, temp, texel);
		}
	}
}

static void
process_idma_entry (hikaru_gpu_t *gpu, uint32_t entry[4])
{
	hikaru_gpu_texture_t texture;
	uint32_t exp_size[2];

	texture.addr	= entry[0];
	texture.size	= entry[1];
	texture.slotx	= entry[2] & 0xFF;
	texture.sloty	= (entry[2] >> 8) & 0xFF;
	texture.width	= 16 << ((entry[2] >> 16) & 7);
	texture.height	= 16 << ((entry[2] >> 19) & 7);
	texture.format	= (entry[2] >> 26) & 7;

	exp_size[0] = texture.width * texture.height * 2;
	exp_size[1] = calc_full_texture_size (&texture);

	/* XXX this check isn't clever enough to figure out that AIRTRIX
	 * texture blobs _do_ have a mipmap. However even if we miss a
	 * mipmap or two, it shouldn't be that big of a problem. */
	texture.has_mipmap = (texture.size == exp_size[1]) ? 1 : 0;

	VK_LOG ("IDMA %08X %08X %08X %08X : %s",
	        entry[0], entry[1], entry[2], entry[3],
	        get_gpu_texture_str (&texture));

	if ((entry[2] & 0xE3C00000) ||
	    (entry[3] & 0xFFFFFFFE)) {
		VK_ERROR ("IDMA: unhandled bits in texture entry: %08X %08X %08X %08X",
		          entry[0], entry[1], entry[2], entry[3]);
		/* continue anyway */
	}

	if (texture.size != exp_size[0] &&
	    texture.size != exp_size[1]) {
		VK_ERROR ("IDMA: unexpected texture size: %s",
		          get_gpu_texture_str (&texture));
		/* continue anyway */
	}

	if (texture.format != FORMAT_RGBA5551 &&
	    texture.format != FORMAT_RGBA4444 &&
	    texture.format != FORMAT_RGBA1111 &&
	    texture.format != FORMAT_ALPHA8) {
		VK_ERROR ("IDMA: unknown texture format, skipping: %s",
		          get_gpu_texture_str (&texture));
		return;
	}

	if ((texture.addr & 0xFE000000) != 0x40000000 &&
	    (texture.addr & 0xFF000000) != 0x48000000) {
		VK_ERROR ("IDMA: unknown texture address, skipping: %s",
		          get_gpu_texture_str (&texture));
		return;
	}

	copy_texture (gpu, &texture);
	hikaru_renderer_register_texture (gpu->renderer, &texture);
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
		/* This should be safe; I don't think it actually gets
		 * overwritten considering that the IRL2 handler does it
		 * itself */
		REG15 (0x14) = 0;
		hikaru_gpu_raise_irq (gpu, _15_IRQ_IDMA, 0);
	}
}

/* TEXRAM-to-TEXRAM DMA */

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

	VK_LOG ("GPU 1A FIFO exec: [%08X %08X %08X %08X] { %u %u } --> { %u %u }, %ux%u",
	        fifo[0], fifo[1], fifo[2], fifo[3],
		src_x, src_y, dst_x, dst_y, w, h);

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			uint32_t src_offs = (src_y + i) * 0x1000 + (src_x + j) * 2;
			uint32_t dst_offs = (dst_y + i) * 0x1000 + (dst_x + j) * 2;
			uint16_t pixel;
			pixel = vk_buffer_get (gpu->texram, 2, src_offs);
			vk_buffer_put (gpu->texram, 2, dst_offs, pixel);
		}
	}

	REG1A (0x24) |= 1;
}

/* Framebuffer/2D Layers
 * =====================
 *
 * Apparently controlled by 1A000100, 1A000180+ and 1A000200+.
 *
 * Size and format: unknown. Speculated to be set by command 021/221.
 */

static void
parse_coords (vec2i_t *out, uint32_t coords)
{
	out->x[0] = (coords & 0x1FF) * 4;
	out->x[1] = (coords >> 9);
}

static void
hikaru_gpu_render_bitmap_layers (hikaru_gpu_t *gpu)
{
	unsigned i, j, offs;
	/* XXX figure out what is the layer priority (fixed?) and how they
	 * are enabled/disabled. */
	/* XXX figure out the proper layer format instead of just ramming
	 * the raw data into the renderer. */
	for (i = 0; i < 2; i++)
		for (j = 0; j < 4; j++) {
			offs = j*8;
			if (REG1AUNIT (i, offs + 0x20) ||
			    REG1AUNIT (i, offs + 0x24)) {
				vec2i_t rect[2];
				uint32_t lo = REG1AUNIT (i, offs);
				uint32_t hi = REG1AUNIT (i, offs + 4);
				parse_coords (&rect[0], lo);
				parse_coords (&rect[1], hi);
				hikaru_renderer_draw_layer (gpu->base.mach->renderer, rect);
			}
		}
}

/* External event handlers */

void
hikaru_gpu_hblank_in (vk_device_t *dev, unsigned line)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	REG1A(0x1C) = (REG1A(0x1C) & ~0x003FF800) | (line << 11);
}

void
hikaru_gpu_vblank_in (vk_device_t *dev)
{
	(void) dev;
}

void
hikaru_gpu_vblank_out (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	hikaru_gpu_raise_irq (gpu, _15_IRQ_VBLANK, _1A_IRQ_VBLANK);
	hikaru_gpu_render_bitmap_layers (gpu);
}

int hikaru_gpu_exec_one (hikaru_gpu_t *gpu);

static int
hikaru_gpu_exec (vk_device_t *dev, int cycles)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	if (!gpu->renderer)
		gpu->renderer = dev->mach->renderer;

	hikaru_gpu_step_idma (gpu);

	/* XXX the second condition shouldn't be needed if is_running is
	 * updated properly... */
	if (!gpu->cs.is_running || REG15 (0x58) != 3)
		return 0;

	/* XXX hack, no idea how fast the GPU is or how much time each
	 * command takes. */
	gpu->cs.cycles = cycles;
	while (gpu->cs.cycles > 0) {
		if (hikaru_gpu_exec_one (gpu)) {
			hikaru_gpu_end_processing (gpu);
			gpu->cs.cycles = 0;
			break;
		}
		gpu->cs.cycles--;
	}

	return 0;
}

/* Accessors */

static int
hikaru_gpu_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	uint32_t *val32 = (uint32_t *) val;

	VK_ASSERT (size == 4 || (size == 2 && addr == 0x15000010));

	*val32 = 0;
	if (addr >= 0x15000000 && addr < 0x15000100) {
		switch (addr & 0xFF) {
		case 0x10:
			if (size == 2) {
				set_ptr (val, 2, REG15 (addr));
				return 0;
			}
		case 0x88:
			break;
		case 0x14:
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
hikaru_gpu_put (vk_device_t *device, size_t size, uint32_t addr, uint64_t val)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) device;

	VK_ASSERT (size == 4);

	if (addr >= 0x15000000 && addr < 0x15000100) {
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
		case 0x58: /* Control */
			REG15 (0x58) = val;
			hikaru_gpu_update_cs_status (gpu);
			return 0;
		case 0x84: /* IRQ mask */
			REG15 (addr) = val;
			hikaru_gpu_update_irqs (gpu);
			return 0;
		case 0x88: /* IRQ status */
			REG15 (addr) &= val;
			hikaru_gpu_update_irqs (gpu);
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
			hikaru_gpu_update_irqs (gpu);
			return 0;
		case 0x24:
			REG1A (addr) = val;
			hikaru_gpu_update_cs_status (gpu);
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

static void
hikaru_gpu_reset (vk_device_t *dev, vk_reset_type_t type)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	memset (gpu->regs_15, 0, 0x100);
	memset (gpu->regs_18, 0, 0x100);
	memset (gpu->regs_1A, 0, 0x104);
	memset (gpu->regs_1A_unit[0], 0, 0x40);
	memset (gpu->regs_1A_unit[0], 0, 0x40);
	memset (gpu->regs_1A_fifo, 0, 0x10);

	memset (&gpu->cs, 0, sizeof (gpu->cs));

	memset (&gpu->viewports, 0, sizeof (gpu->viewports));
	memset (&gpu->materials, 0, sizeof (gpu->materials));
	memset (&gpu->texheads, 0, sizeof (gpu->texheads));
	memset (&gpu->lights, 0, sizeof (gpu->lights));
	memset (&gpu->mtx, 0, sizeof (gpu->mtx));

	gpu->viewports.scratch.used = 1;
	gpu->materials.scratch.used = 1;
	gpu->texheads.scratch.used = 1;
}

const char *
hikaru_gpu_get_debug_str (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	static char out[256];

	sprintf (out, "@%08X %u 15:58=%u 1A:24=%u 15:84=%X 15:88=%X 1A:18=%X",
	         gpu->cs.pc, (unsigned) gpu->cs.is_running,
	         REG15 (0x58), REG1A (0x24),
	         REG15 (0x84), REG15 (0x88), REG1A (0x18));

	return out;
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

char *
get_gpu_viewport_str (hikaru_gpu_viewport_t *viewport)
{
	static char out[512];
	sprintf (out, "(%7.3f %7.3f %7.3f) (%u,%u) (%u,%u) (%u,%u) (%u %5.3f %5.3f) (%u %5.3f %5.3f)",
	         viewport->persp_x, viewport->persp_y, viewport->persp_unk,
	         viewport->center.x[0], viewport->center.x[1],
	         viewport->extents_x.x[0], viewport->extents_x.x[1],
	         viewport->extents_y.x[0], viewport->extents_y.x[1],
	         viewport->depth_func, viewport->depth_near, viewport->depth_far,
	         viewport->depthq_type, viewport->depthq_density, viewport->depthq_bias);
	return out;
}

char *
get_gpu_material_str (hikaru_gpu_material_t *material)
{
	static char out[512];
	sprintf (out, "C0=#%02X%02X%02X C1=#%02X%02X%02X S=%u,#%02X%02X%02X MC=#%04X,%04X,%04X M=%u Z=%u T=%u A=%u H=%u B=%u",
	         material->color[0].x[0],
	         material->color[0].x[1],
	         material->color[0].x[2],
	         material->color[1].x[0],
	         material->color[1].x[1],
	         material->color[1].x[2],
	         material->specularity,
	         material->shininess.x[0],
	         material->shininess.x[1],
	         material->shininess.x[2],
	         material->material_color.x[0],
	         material->material_color.x[1],
	         material->material_color.x[2],
	         material->mode,
	         material->depth_blend,
	         material->has_texture,
	         material->has_alpha,
	         material->has_highlight,
	         material->bmode);
	return out;
}

char *
get_gpu_texhead_str (hikaru_gpu_texhead_t *texhead)
{
	static const char *name[8] = {
		"RGBA5551",
		"RGBA4444",
		"RGBA1111",
		"3",
		"ALPHA8",
		"5",
		"6",
		"7"
	};
	static char out[512];
	sprintf (out, "slot=%X,%X pos=%X,%X offs=%08X %ux%u %s ni=%X by=%X u4=%X u8=%X uk=%X",
	         texhead->slotx, texhead->sloty,
	         texhead->slotx*8, texhead->sloty*8,
	         texhead->sloty*8*4096+texhead->slotx*8*2,
	         texhead->width, texhead->height,
	         name[texhead->format],
	         texhead->_0C1_nibble, texhead->_0C1_byte,
	         texhead->_2C1_unk4, texhead->_2C1_unk8,
	         texhead->_4C1_unk);
	return out;
}

char *
get_gpu_texture_str (hikaru_gpu_texture_t *texture)
{
	static char out[256];
	sprintf (out, "%X size=%X slot=%X,%X pos=%X,%X offs=%08X %ux%u format=%u hasmip=%u",
	         texture->addr, texture->size,
	         texture->slotx, texture->sloty,
	         texture->slotx*8, texture->sloty*8,
	         texture->sloty*8*4096+texture->slotx*8*2,
	         texture->width, texture->height, texture->format,
	         texture->has_mipmap);
	return out;
}

static void
hikaru_gpu_print_rendering_state (hikaru_gpu_t *gpu)
{
	unsigned i;

	for (i = 0; i < NUM_VIEWPORTS; i++) {
		hikaru_gpu_viewport_t *vp;
		vp = &gpu->viewports.table[i];
		if (vp->used)
			VK_LOG ("GPU RS: viewport %3u: %s", i,
			        get_gpu_viewport_str (vp));
	}
	for (i = 0; i < NUM_MATERIALS; i++) {
		hikaru_gpu_material_t *mat;
		mat = &gpu->materials.table[i];
		if (mat->used)
			VK_LOG ("GPU RS: material %3u: %s", i,
			        get_gpu_material_str (mat));
	}
	for (i = 0; i < NUM_TEXHEADS; i++) {
		hikaru_gpu_texhead_t *th;
		th = &gpu->texheads.table[i];
		if (th->used)
			VK_LOG ("GPU RS: texhead %3u: %s", i,
			        get_gpu_texhead_str (th));
	}
}

static void
hikaru_gpu_delete (vk_device_t **dev_)
{
	if (dev_) {
		hikaru_gpu_t *gpu = (hikaru_gpu_t *) *dev_;
		hikaru_gpu_print_rendering_state (gpu);
		free (gpu);
		*dev_ = NULL;
	}
}

vk_device_t *
hikaru_gpu_new (vk_machine_t *mach, vk_buffer_t *cmdram, vk_buffer_t *texram)
{
	hikaru_gpu_t *gpu = ALLOC (hikaru_gpu_t);
	if (gpu) {
		gpu->base.mach = mach;

		gpu->base.delete	= hikaru_gpu_delete;
		gpu->base.reset		= hikaru_gpu_reset;
		gpu->base.exec		= hikaru_gpu_exec;
		gpu->base.get		= hikaru_gpu_get;
		gpu->base.put		= hikaru_gpu_put;
		gpu->base.save_state	= hikaru_gpu_save_state;
		gpu->base.load_state	= hikaru_gpu_load_state;

		gpu->cmdram = cmdram;
		gpu->texram = texram;
	}
	return (vk_device_t *) gpu;
}
