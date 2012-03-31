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
#include "mach/hikaru/hikaru-renderer.h"
#include "cpu/sh/sh4.h"

/* TODO: figure out what is 4Cxxxxxx */
/* TODO: handle slave access */

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
 *    stack somewhere (still to figure out where, though.)
 *
 *    My guess is that even and odd frames are processed by two different,
 *    identical processors.
 *
 *    The device is likely controlled by the MMIOs at 1500007x. The code
 *    is held in CMDRAM, which is at 14300000-143FFFFF in the master SH-4
 *    address space, and 48000000-483FFFFF in bus address space.
 *
 *  - An indirect DMA-like device which is likely used to move texture
 *    data to/from TEXRAM, and is able to decode between texture formats on
 *    the fly.
 *
 *    The device can be accessed thru the MMIOs at 150000(0C|10|14).
 *
 *  - A FIFO-like device, used to move textures around in TEXRAM. In
 *    particular, it is used to transfer bitmap data directly to the
 *    framebuffer(s).
 *
 *    The device can be accessed thru the MMIOs at 1A0400xx.
 *
 * Matrices
 * ========
 *
 * The hardware uses 4x3 matrices (see the 161 command), with the fourth
 * vector specifying translation.
 *
 * Textures
 * ========
 *
 * Texture come in at least the following formats: RGBA8888, RGBA551, RGB565,
 * RGBA4444, and possibly alpha/luminance-only textures (as seen in the
 * ROM data.)
 *
 * Textures also come with mipmap trees.
 */

#define NUM_VIEWPORTS	32
#define NUM_MATERIALS	256
#define NUM_TEXHEADS	256
#define NUM_MATRICES	16

#define VIEWPORT_MASK	(NUM_VIEWPORTS - 1)
#define MATERIAL_MASK	(NUM_MATERIALS - 1)
#define TEXHEAD_MASK	(NUM_TEXHEADS - 1)
#define MATRIX_MASK	(NUM_MATRICES - 1)

typedef struct {
	/* 021 */
	float persp_x;
	float persp_y;
	float persp_unk;
	/* 221 */
	vec2s_t center;
	vec2s_t extents_x;
	vec2s_t extents_y;
	/* 421 */
	unsigned unk_func;
	float unk_n;
	float unk_b;
	/* 621 */
	uint32_t depth_type;
	uint32_t depth_enabled;
	uint32_t depth_unk;
	vec4b_t	depth_mask;
	float depth_density;
	float depth_bias;
	/* 881 */
	vec3s_t ambient_color;
	/* 991 */
	vec4b_t clear_color;
} viewport_state_t;

typedef struct {
	/* 091, 291 */
	vec3b_t color[2];
	/* 491 */
	vec4b_t shininess;
	/* 691 */
	vec3s_t material_color;
	/* 081 */
	/* XXX unknown */
	/* 881 */
	unsigned mode		: 2;
	unsigned depth_blend	: 1;
	unsigned has_texture	: 1;
	unsigned has_alpha	: 1;
	unsigned has_highlight	: 1;
	/* A81 */
	unsigned bmode		: 2;
	/* C81 */
	/* XXX unknown */
} material_state_t;

typedef struct {
	/* 0C1 */
	uint8_t _0C1_n : 4;
	uint8_t _0C1_m : 4;
	/* 2C1 */
	uint8_t _2C1_a;
	uint8_t _2C1_b;
	uint8_t _2C1_u : 4;
	/* 4C1 */
	uint8_t _4C1_n;
	uint8_t _4C1_m;
	uint8_t _4C1_p : 4;
} texhead_state_t;

typedef struct {
	vk_device_t base;

	vk_buffer_t *cmdram;
	vk_buffer_t *texram;

	uint8_t regs_15[0x100];
	uint8_t regs_18[0x100];
	uint8_t regs_1A[0x104];
	uint8_t regs_1A_unit[2][0x40];
	uint8_t regs_1A_fifo[0x10];

	bool is_running;

	unsigned frame_type;
	uint32_t pc;
	uint32_t sp[2];
	int cycles;

	viewport_state_t vp_scratch;
	viewport_state_t vp[NUM_VIEWPORTS];
	viewport_state_t *current_vp;
	int current_vp_offset;

	material_state_t ms_scratch;
	material_state_t ms[NUM_MATERIALS];
	material_state_t *current_ms;
	int current_ms_offset;

	texhead_state_t ts_scratch;
	texhead_state_t ts[NUM_TEXHEADS];
	texhead_state_t *current_ts;
	int current_ts_offset;

	mtx4x3f_t mtx_scratch;
	mtx4x3f_t mtx[NUM_MATRICES];

	/* XXX ditch immediate mode and replace with cached models */
	vec3f_t vertex_buffer[3];
	int vertex_index;

} hikaru_gpu_t;

/*
 * GPU Address Space
 * =================
 *
 * The GPU has access to the whole external BUS address space. See
 * hikaru-memctl.c for more details.
 *
 * GPU MMIOs at 15000000
 * =====================
 *
 * Note: all ports are 32-bit wide, unless otherwise noted.
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
 * Indirect DMA/Texture Conversion MMIOs
 * -------------------------------------
 *
 * 1500000C   W		Indirect DMA table address (in CMDRAM)
 * 15000010  RW		Indirect DMA # of entries to process (also 16 bit)
 * 15000014  RW		Indirect DMA Control
 *			 Bit 0: exec when set, busy when read
 *
 * Note: it may be related to vblank timing, see PH:@0C0128E6 and
 * PH:@0C01290A.
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
 * Note: same as Config A, plus an offset of +80000 or +8000.
 * Note: 000FFE00 is so so similar to 120FEE00 ~ 483FB800 used in the BOOTROM
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
 * 15000074   W		CS Processor 0 SP; = 483F0100
 * 15000078   W		CS Processor 1 SP; = 483F8100
 * 1500007C   W		CS Abort
 *			 Execution when flipped 0, 1 are written?
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
 *			   with bit 0 of 1A000024), see @0C0069E0.
 *			 - determine the HARDWARE VERSION:
 *				 - 0=older
 *				 - 1=newer
 *			   See @0C001AC4, PH:@0C01220C
 *
 * GPU Performance Counters
 * ------------------------
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
 * Note: when any of these bits is set, bit 7 of 15000088 is set.
 * Note: it may be related to 1A0000C4, see @0C001ED0.
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
 *	   		Notes:
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
 * Note: my gutter feeling is that these register specify operations that must
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
 * 1A000100             l RW    Enable scanout (the framebuffer is displayed
 *				on-screen.)
 *				See @0C007D00 ,PH:@0C01A0F8, PH@0C01A10C,
 *
 * Texture RAM Control A & B
 * -------------------------
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
 *      Related to TEXTURING.
 *
 * Unknown
 * -------
 *
 * 1A020000 32-bit  W	"SEGA" is written here; see @0C001A58
 *
 * TEXRAM to TEXRAM Copy Engine
 * ----------------------------
 *
 * 1A040000 32-bit  W	Source coords
 * 1A040004 32-bit  W	Destination coords
 * 1A040008 32-bit  W	Texture Size
 * 1A04000C 32-bit  W	Control
 *
 * Unknown
 * -------
 *
 * 1A08006C 32-bit R	Unknown
 *
 * 1A0A1600             l  W    1 [seems related to 15040E00, see pharrier]
 */

#define REG15(addr_)	(*(uint32_t *) &gpu->regs_15[(addr_) & 0xFF])
#define REG18(addr_)	(*(uint32_t *) &gpu->regs_18[(addr_) & 0xFF])
#define REG1A(addr_)	(*(uint32_t *) &gpu->regs_1A[(addr_) & 0x1FF])
#define REG1AUNIT(n,a)	(*(uint32_t *) &gpu->regs_1A_unit[n][(a) & 0x3F])
#define REG1AFIFO(a)	(*(uint32_t *) &gpu->regs_1A_fifo[(a) & 0xF])

const char *
hikaru_gpu_get_debug_str (vk_device_t *dev)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;
	static char out[256];

	sprintf (out, "@%08X %u 15:58=%u 1A:24=%u 15:84=%X 15:88=%X 1A:18=%X",
	         gpu->pc, (unsigned) gpu->is_running,
	         REG15 (0x58), REG1A (0x24),
	         REG15 (0x84), REG15 (0x88), REG1A (0x18));

	return out;
}

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
	//VK_CPU_LOG (cpu, " ### raising GPU IRQs 15:%02X 1A:%02X", _15, _1A);
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

/* Rendering Helpers */

static void
append_vertex (hikaru_gpu_t *gpu, vec3f_t *src)
{
	gpu->vertex_buffer[gpu->vertex_index] = *src;
	gpu->vertex_buffer[gpu->vertex_index].x[1] += 480.0f; /* XXX hack */
	gpu->vertex_index = (gpu->vertex_index + 1) % 3;
}

static int
get_vertex_index (int i)
{
	return (i < 0) ? (i + 3) : i;
}

static void
draw_tri (hikaru_gpu_t *gpu, vec2s_t *uv0, vec2s_t *uv1, vec2s_t *uv2)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) gpu->base.mach->renderer;

	int i0 = get_vertex_index (gpu->vertex_index - 1);
	int i1 = get_vertex_index (gpu->vertex_index - 2);
	int i2 = get_vertex_index (gpu->vertex_index - 3);

	hikaru_renderer_draw_tri (hr,
	                          &gpu->vertex_buffer[i0],
	                          &gpu->vertex_buffer[i1],
	                          &gpu->vertex_buffer[i2],
	                          true,
	                          gpu->current_ms->color[1],
	                          gpu->current_ms->has_texture,
	                          uv0, uv1, uv2);
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
 * A new frame subroutine is uploaded when both IRQs 2 of GPU 15 and GPU 1A
 * are fired, meaning that they both consumed the data passed in and require
 * new data (subroutine) to continue processing.
 *
 * When execution ends:
 * CHECK ALL THIS AGAIN PLEASE
 *  1A00000C bit 0 is set
 *  1A000018 bit 1 is set as a consequence
 *  15000088 bit 7 is set as a consequence
 *  15000088 bit 1 is set; a GPU IRQ is raised (if not masked by 15000084)
 *  15002000 bit 0 is set on some HW revisions
 *  1A000024 bit 0 is cleared if some additional condition occurred
 *
 * 15002000 and 1A000024 signal different things; see the termination
 * condition in sync_for_frame ()
 */

/* NOTE: it looks like the RECALL opcodes actually set the current offset
 * for the following SET PROPERTY instructions. See PHARRIER. */

/* For an additional command 02A: See PH:@0C0DECC0 */

static bool
cp_is_valid_addr (uint32_t addr)
{
	return (addr >= 0x40000000 && addr <= 0x41FFFFFF) ||
	       (addr >= 0x48000000 && addr <= 0x483FFFFF) ||
	       (addr >= 0x4C000000 && addr <= 0x4C3FFFFF);
}

static void
cp_push_pc (hikaru_gpu_t *gpu)
{
	unsigned i = gpu->frame_type;
	vk_buffer_put (gpu->cmdram, 4, gpu->sp[i] & 0xFFFFFF, gpu->pc);
	gpu->sp[i] -= 4;
}

static void
cp_pop_pc (hikaru_gpu_t *gpu)
{
	unsigned i = gpu->frame_type;
	gpu->sp[i] += 4;
	gpu->pc = vk_buffer_get (gpu->cmdram, 4, gpu->sp[i] & 0xFFFFFF) + 8;
}

static int
exp16 (int x)
{
	if (x == 0)
		return 1;
	return 0x10 << x;
}

#define ASSERT(cond_) \
	do { \
		if (!(cond_)) { \
			VK_ABORT ("GPU: @%08X: assertion failed, aborting", gpu->pc); \
		} \
	} while (0);

static void
read_inst (vk_buffer_t *buf, uint32_t *inst, uint32_t offs)
{
	unsigned i;
	/* XXX this is not exactly ideal; change the CMDRAM to an uint32_t
	 * buffer */
	for (i = 0; i < 8; i++)
		inst[i] = vk_buffer_get (buf, 4, offs + i * 4);
}

static int
hikaru_gpu_exec_one (hikaru_gpu_t *gpu)
{
	vk_device_t *device = (vk_device_t *) gpu;
	hikaru_t *hikaru = (hikaru_t *) device->mach;
	uint32_t inst[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	ASSERT (cp_is_valid_addr (gpu->pc));
	ASSERT (cp_is_valid_addr (gpu->sp[0]));
	ASSERT (cp_is_valid_addr (gpu->sp[1]));

	switch (gpu->pc >> 24) {
	case 0x40:
	case 0x41:
		read_inst (hikaru->ram_s, inst, gpu->pc & 0x01FFFFFF);
		break;
	case 0x48:
	case 0x4C:
		read_inst (hikaru->cmdram, inst, gpu->pc & 0x00FFFFFF);
		break;
	}

	unsigned op = inst[0] & 0xFFF;
	switch (op) {

	/* Flow Control */

	case 0x000:
		/* 000	Nop
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 */
		VK_LOG ("GPU CMD %08X: Nop [%08X]", gpu->pc, inst[0]);
		ASSERT (inst[0] == 0);
		gpu->pc += 4;
		break;
	case 0x012:
		/* 012	Jump
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa		a = Address in 32-bit words
		 */
		{
			uint32_t addr = inst[1] * 4;
			VK_LOG ("GPU CMD %08X: Jump [%08X] %08X",
			        gpu->pc, inst[0], addr);
			ASSERT (inst[0] == 0x12);
			gpu->pc = addr;
		}
		break;
	case 0x812:
		/* 812	Jump Rel
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa		a = Offset in 32-bit words
		 */
		{
			uint32_t addr = gpu->pc + inst[1] * 4;
			VK_LOG ("GPU CMD %08X: Jump Rel [%08X %08X] %08X",
			        gpu->pc, inst[0], inst[1], addr);
			ASSERT (inst[0] == 0x812);
			gpu->pc = addr;
		}
		break;
	case 0x052:
		/* 052	Call
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa		a = Address in 32-bit words
		 */
		{
			uint32_t addr = inst[1] * 4;
			VK_LOG ("GPU CMD %08X: Call [%08X] %08X",
			        gpu->pc, inst[0], addr);
			ASSERT (inst[0] == 0x52);
			cp_push_pc (gpu);
			gpu->pc = addr;

		}
		break;
	case 0x852:
		/* 852	Call Rel
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa		a = Offset in 32-bit words
		 */
		{
			uint32_t addr = gpu->pc + inst[1] * 4;
			VK_LOG ("GPU CMD %08X: Jump Rel [%08X %08X] %08X",
			        gpu->pc, inst[0], inst[1], addr);
			ASSERT (inst[0] == 0x852);
			cp_push_pc (gpu);
			gpu->pc = addr;
		}
		break;
	case 0x082:
		/* 082	Return
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 */
		VK_LOG ("GPU CMD %08X: Return [%08X]",
		        gpu->pc, inst[0]);
		ASSERT (inst[0] == 0x82);
		cp_pop_pc (gpu);
		break;
	case 0x1C2:
		/* 1C2	Kill
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 */
		VK_LOG ("GPU CMD %08X: Kill [%08X]",
		        gpu->pc, inst[0]);
		ASSERT (inst[0] == 0x1C2);
		gpu->is_running = false;
		gpu->pc += 4;
		return 1;

	/* Frame Control */

	case 0x781:
		/* 781	Sync (or Dispatch to child GPU)
		 *
		 *	---- aabb ---- mmnn ---- oooo oooo oooo		o = Opcode, a, b, m, n = Unknown
		 *
		 * See @0C0065D6, PH:@0C016336.
		 */
		{
			unsigned a, b, m, n;
			a = (inst[0] >> 26) & 3;
			b = (inst[0] >> 24) & 3;
			m = (inst[0] >> 18) & 3;
			n = (inst[0] >> 16) & 3;

			VK_LOG ("GPU CMD %08X: Sync [%08X] <%u %u %u %u>",
			        gpu->pc, inst[0], a, b, n, m);

			gpu->pc += 4;
		}
		break;

	/* Clear Primitives */

	case 0x154:
		/* 154	Clear Unknown A
		 *
		 *	nnnn nnnn nnnn nnnn ---- oooo oooo oooo
		 *	---- ---- ---- ---- hhhh hhhh llll llll
		 *
		 * l = Alpha low threshold
		 * h = Alpha high threshold
		 *
		 * See PH:@0C017798, PH:@0C0CF868.
		 */
		{
			unsigned n, a, b;
			n = (inst[0] >> 16) & 0xFF;
			a = inst[1] & 0xFF;
			b = (inst[1] >> 8) & 0xFF;
			VK_LOG ("GPU CMD %08X: Clear Unknown A [%08X %08X] %u <%X %X>",
			        gpu->pc, inst[0], inst[1], n, a, b);
			gpu->pc += 8;
		}
		break;
	case 0x194:
		/* 194	Clear Unknown B */
		{
			unsigned n, m, a, b;
			n = (inst[0] >> 16) & 0xFF;
			m = (inst[0] >> 24) & 0xFF;
			a = inst[1] & 0xFFFF;
			b = inst[1] >> 16;
			VK_LOG ("GPU CMD %08X: Clear Unknown B [%08X %08X] %u %u <%X %X>",
			        gpu->pc, inst[0], inst[1], n, m, a, b);
			gpu->pc += 8;
		}
		break;

	/* Viewport
	 * ========
	 *
	 * The parameters for many of these are taken from PHARRIER debug
	 * info.
	 */

	case 0x021:
		/* 021	Set Viewport Projection
		 *
		 * 	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *      pppp pppp pppp pppp pppp pppp pppp pppp		p = alpha * cotf (angle / 2)
		 *      qqqq qqqq qqqq qqqq qqqq qqqq qqqq qqqq		q =  beta * cotf (angle / 2)
		 *      zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz		z = Depth component, float
		 *
		 * See PH:@0C01587C, PH:@0C0158A4, PH:@0C0158E8.
		 */
		gpu->vp_scratch.persp_x		= *(float *) &inst[1];
		gpu->vp_scratch.persp_y		= *(float *) &inst[2];
		gpu->vp_scratch.persp_unk	= *(float *) &inst[3];

		VK_LOG ("GPU CMD %08X: Viewport: Set Projection [%08X %08X %08X %08X] px=%f py=%f unk=%f",
		        gpu->pc, inst[0], inst[1], inst[2], inst[3],
		        gpu->vp_scratch.persp_x,
		        gpu->vp_scratch.persp_y,
		        gpu->vp_scratch.persp_unk);

		gpu->pc += 16;
		break;
	case 0x221:
		/* 221	Viewport: Set Extents
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	jjjj jjjj jjjj jjjj cccc cccc cccc cccc		c = X center; j = Y center
		 *	--YY YYYY YYYY YYYY -XXX XXXX XXXX XXXX		Y, X = Coord maximum; Y can be at most 512, X can be at most 640
		 *	--yy yyyy yyyy yyyy -xxx xxxx xxxx xxxx		y, x = Coord minimums; at least one of them MUST be zero
		 *
		 * See PH:@0C015924
		 */
		gpu->vp_scratch.center.x[0]	= inst[1] & 0xFFFF;
		gpu->vp_scratch.center.x[1]	= inst[1] >> 16;
		gpu->vp_scratch.extents_x.x[0]	= inst[2] & 0x7FFF;
		gpu->vp_scratch.extents_x.x[1]	= inst[3] & 0x7FFF;
		gpu->vp_scratch.extents_y.x[0]	= (inst[2] >> 16) & 0x3FFF;
		gpu->vp_scratch.extents_y.x[1]	= (inst[3] >> 16) & 0x3FFF;

		VK_LOG ("GPU CMD %08X: Viewport: Set Extents [%08X %08X %08X %08X] center=<%u,%u> x=<%u,%u> y=<%u,%u> ]",
		        gpu->pc, inst[0], inst[1], inst[2], inst[3],
		        gpu->vp_scratch.center.x[0],
		        gpu->vp_scratch.center.x[1],
		        gpu->vp_scratch.extents_x.x[0],
		        gpu->vp_scratch.extents_x.x[1],
		        gpu->vp_scratch.extents_y.x[0],
		        gpu->vp_scratch.extents_y.x[1])

		ASSERT (!(inst[2] & 0xC0008000));
		ASSERT (!(inst[3] & 0xC0008000));

		gpu->pc += 16;
		break;
	case 0x421:
		/* 421	Viewport: Set Unknown
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx		x = Unknown
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy		y = Unknown
		 *	aaa- ---- ---- ---- ---- ---- ---- ----		a = Unknown
		 *
		 * See PH:@0C015AA6
		 */
		gpu->vp_scratch.unk_func = inst[3] >> 29;
		gpu->vp_scratch.unk_n = *(float *) &inst[1];
		gpu->vp_scratch.unk_b = *(float *) &inst[2];

		VK_LOG ("GPU CMD %08X: Viewport: Set Unknown [%08X %08X %08X %08X] func=%u n=%f b=%f",
		        gpu->pc, inst[0], inst[1], inst[2], inst[3],
		        gpu->vp_scratch.unk_func,
		        gpu->vp_scratch.unk_n,
		        gpu->vp_scratch.unk_b);
			        
		ASSERT (!(inst[3] & 0x1FFFFFFF));

		gpu->pc += 16;
		break;
	case 0x621:
		/* 621	Viewport: Set Depth
		 *
		 *	---- ---- ---- ttDu ---- oooo oooo oooo
		 *      AAAA AAAA BBBB BBBB GGGG GGGG RRRR RRRR
		 *	dddd dddd dddd dddd dddd dddd dddd dddd 
		 *	gggg gggg gggg gggg gggg gggg gggg gggg
		 *
		 * t = Depth type (function)
		 * D = Disable depth test?
		 * u = Unknown
		 * RGBA = Depth mask?
		 * f = Depth density [1]
		 * g = Depth bias [2]
		 *
		 * [1] Computed as 1.0f (constant), 1.0f / zdelta, or
		 *     1.0f / sqrt (zdelta**2); where zdelta = zend - zstart.
		 *
		 * [2] Computed as kappa / zstart. The value kappa is
		 *     stored in (13, GBR) aka 0C00F034.
		 *
		 * See PH:@0C0159C4, PH:@0C015A02, PH:@0C015A3E.
		 */
		gpu->vp_scratch.depth_type	= (inst[0] >> 18) & 3;
		gpu->vp_scratch.depth_enabled	= ((inst[0] >> 17) & 1) ? 0 : 1;
		gpu->vp_scratch.depth_unk	= (inst[0] >> 16) & 1;
		gpu->vp_scratch.depth_mask.x[0]	= inst[1] & 0xFF;
		gpu->vp_scratch.depth_mask.x[1]	= (inst[1] >> 8) & 0xFF;
		gpu->vp_scratch.depth_mask.x[2]	= (inst[1] >> 16) & 0xFF;
		gpu->vp_scratch.depth_mask.x[3]	= inst[1] >> 24;
		gpu->vp_scratch.depth_density	= *(float *) &inst[2];
		gpu->vp_scratch.depth_bias	= *(float *) &inst[3];

		VK_LOG ("GPU CMD %08X: Viewport: Set Unknown [%08X %08X %08X %08X] type=%u enabled=%u unk=%u mask=<%X %X %X %X> density=%f bias=%f",
		        gpu->pc, inst[0], inst[1], inst[2], inst[3],
			gpu->vp_scratch.depth_type,
			gpu->vp_scratch.depth_enabled,
			gpu->vp_scratch.depth_unk,
			gpu->vp_scratch.depth_mask.x[0],
			gpu->vp_scratch.depth_mask.x[1],
			gpu->vp_scratch.depth_mask.x[2],
			gpu->vp_scratch.depth_mask.x[3],
			gpu->vp_scratch.depth_density,
			gpu->vp_scratch.depth_bias);

		gpu->pc += 16;
		break;
	case 0x811:
		/* 811	Viewport: Set Ambient Color
		 *
		 *	rrrr rrrr rrrr rrrr ---- oooo oooo oooo
		 *	bbbb bbbb bbbb bbbb gggg gggg gggg gggg
		 *
		 * See PH:@0C037840.
		 */
		{
			vec3s_t color;

			color.x[0] = inst[0] >> 16;
			color.x[1] = inst[1] & 0xFFFF;
			color.x[2] = inst[1] >> 16;

			gpu->vp_scratch.ambient_color = color;

			VK_LOG ("GPU CMD %08X: Viewport: Set Ambient Color [%08X %08X] color=<%u %u %u>",
			        gpu->pc, inst[0], inst[1],
			        color.x[2], color.x[1], color.x[0]);

			gpu->pc += 8;
		}
		break;
	case 0x991:
		/* 991	Viewport: Set Clear Color
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	---- ---a gggg gggg bbbb bbbb rrrr rrrr		a,r,g,b = Clear color
		 *
		 * Note: yes, apparently blue and green _are_ swapped.
		 *
		 * XXX double check the alpha mask.
		 *
		 * See PH:@0C016368, PH:@0C016396, PH:@0C037760.
		 */
		{
			vec4b_t color;

			color.x[0] = inst[1] & 0xFF;
			color.x[1] = (inst[1] >> 8) & 0xFF;
			color.x[2] = (inst[1] >> 16) & 0xFF;
			color.x[3] = ((inst[1] >> 24) & 1) ? 0xFF : 0;

			gpu->vp_scratch.clear_color = color;

			VK_LOG ("GPU CMD %08X: Viewport: Set Clear Color [%08X %08X] color=<%u %u %u %u>",
			        gpu->pc, inst[0], inst[1],
			        color.x[3], color.x[2], color.x[1], color.x[0]); 

			gpu->pc += 8;
		}
		break;
	case 0x004:
		/* 004	Commit Viewport
		 *
		 *	---- ---- ---- nnnn ---- oooo oooo oooo
		 *
		 * n = Num; can't be zero
		 *
		 * See PH:@0C015AD0.
		 */
		{
			unsigned n = (inst[0] >> 16) & VIEWPORT_MASK;

			gpu->vp[n] = gpu->vp_scratch;

			VK_LOG ("GPU CMD %08X: Commit Viewport [%08X] %u",
			        gpu->pc, inst[0], n);

			VK_ASSERT (n != 0);
			gpu->pc += 4;
		}
		break;
	case 0x003:
		/* 003	Recall Viewport
		 *
		 *	---- ---- ---- nnnn ---- oooo oooo oooo
		 *
		 * n = Num
		 *
		 * See PH:@0C015AF6, PH:@0C015B12, PH:@0C015B32.
		 */
		{
			unsigned n = (inst[0] >> 16) & VIEWPORT_MASK;

			/* XXX add offset-only mode */
			gpu->current_vp = &gpu->vp[n];

			VK_LOG ("GPU CMD %08X: Recall Viewport [%08X] <%u %u %u>",
			        gpu->pc, inst[0], n);

			VK_ASSERT (n != 0);
			gpu->pc += 4;
		}
		break;

	/* Material
	 * ========
	 *
	 * The parameters for many of these are taken from PHARREIR debug
	 * info.
	 */

	case 0x091:
	case 0x291:
		/* 091	Material: Set Color 0
		 * 291	Material: Set Color 1
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 *	---- ---- bbbb bbbb gggg gggg rrrr rrrr
		 *
		 * See PH:@0C0CF742.
		 */
		{
			vec3b_t color;
			unsigned i = (op >> 9) & 1;

			color.x[0] = inst[1] & 0xFF;
			color.x[1] = (inst[1] >> 8) & 0xFF;
			color.x[2] = (inst[1] >> 16) & 0xFF;

			gpu->ms_scratch.color[i] = color;

			VK_LOG ("GPU CMD %08X: Material: Set Color %X [%08X %08X] <R=%u G=%u B=%u>",
			        gpu->pc, i, inst[0], inst[1],
			        color.x[0], color.x[1], color.x[2]);

			gpu->pc += 8;
		}
		break;
	case 0x491:
		/* 491	Material: Set Shininess
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 *	ssss ssss bbbb bbbb gggg gggg rrrr rrrr
		 *
		 * s = Specular shininess
		 *
		 * See PH:@0C0CF798, PH:@0C01782C.
		 */
		{
			vec4b_t shininess;

			shininess.x[0] = inst[1] & 0xFF;
			shininess.x[1] = (inst[1] >> 8) & 0xFF;
			shininess.x[2] = (inst[1] >> 16) & 0xFF;
			shininess.x[3] = inst[1] >> 24;

			gpu->ms_scratch.shininess = shininess;

			VK_LOG ("GPU CMD %08X: Material: Set Shininess [%08X %08X] specularity=%u shininess=<%u %u %u>",
			        gpu->pc, inst[0], inst[1],
			        shininess.x[3], shininess.x[2],
			        shininess.x[1], shininess.x[0]);

			gpu->pc += 8;
		}
		break;
	case 0x691:
		/* 691	Material: Set Material Color
		 *
		 *	rrrr rrrr rrrr rrrr ---- oooo oooo oooo
		 *	bbbb bbbb bbbb bbbb gggg gggg gggg gggg
		 *
		 * See PH:@0C0CF7CC.
		 */
		{
			vec3s_t color;

			color.x[0] = inst[0] >> 16;
			color.x[1] = inst[1] & 0xFFFF;
			color.x[2] = inst[1] >> 16;

			gpu->ms_scratch.material_color = color;

			VK_LOG ("GPU CMD %08X: Material: Set Material Color [%08X %08X] color=<%u %u %u>",
			        gpu->pc, inst[0], inst[1],
			        color.x[0], color.x[1], color.x[2]);

			gpu->pc += 8;
		}
		break;
	case 0x081:
		/* 081	Material: Set Unknown
		 *
		 *	---- ---- ---- mmmm ---n oooo oooo oooo
		 */
		{
			unsigned n = (inst[0] >> 12) & 1;
			unsigned m = (inst[0] >> 16) & 0xF;

			VK_LOG ("GPU CMD %08X: Material: Set Unknown [%08X] %u %u",
			        gpu->pc, inst[0], n, m);

			gpu->pc += 4;
		}
		break;
	case 0x881:
		/* 881	Set Flags
		 *
		 *	---- ---- --ha tzmm ---- oooo oooo oooo
		 *
		 * m = Mode
		 * z = Depth blend (fog)
		 * t = Enable texture
		 * a = Alpha mode
		 * h = Highlight mode
		 *
		 * See PH:@0C0CF700.
		 */
		gpu->ms_scratch.mode		= (inst[0] >> 16) & 3;
		gpu->ms_scratch.depth_blend	= (inst[0] >> 18) & 1;
		gpu->ms_scratch.has_texture	= (inst[0] >> 19) & 1;
		gpu->ms_scratch.has_alpha	= (inst[0] >> 20) & 1;
		gpu->ms_scratch.has_highlight	= (inst[0] >> 21) & 1;

		VK_LOG ("GPU CMD %08X: Material: Set Flags [%08X] mode=%u zblend=%u tex=%u alpha=%u highl=%u",
		        gpu->pc, inst[0],
			gpu->ms_scratch.mode,
			gpu->ms_scratch.depth_blend,
			gpu->ms_scratch.has_texture,
			gpu->ms_scratch.has_alpha,
			gpu->ms_scratch.has_highlight);

		gpu->pc += 4;
		break;
	case 0xA81:
		/* A81	Material: Set BMode
		 *
		 *	---- ---- ---- ---mm ---- oooo ooo oooo
		 *
		 * See PH:@0C0CF7FA.
		 */
		gpu->ms_scratch.bmode = (inst[0] >> 16) & 3;

		VK_LOG ("GPU CMD %08X: Material: Set BMode [%08X]", gpu->pc, inst[0]);

		gpu->pc += 4;
		break;
	case 0xC81:
		/* C81	Material: Set Unknown
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 */

		VK_LOG ("GPU CMD %08X: Material: Set Unknown [%08X]", gpu->pc, inst[0]);

		gpu->pc += 4;
		break;
	case 0x084:
		/* 084	Commit Material
		 *
		 *	---- ---- nnnn nnnn ---m oooo oooo oooo		o = Opcode, n = Number
		 *
		 * See PH:@0C0153D4, PH:@0C0CF878.
		 */
		{
			unsigned num = (inst[0] >> 16) & MATERIAL_MASK;
			unsigned unk = (inst[0] >> 12) & 1;

			gpu->ms[num] = gpu->ms_scratch;

			VK_LOG ("GPU CMD %08X: Commit Material [%08X] num=%u unk=%u",
			        gpu->pc, inst[0], num, unk);

			gpu->pc += 4;
		}
		break;
	case 0x083:
		/* 083	Recall Material
		 *
		 *	uuuu uuuu nnnn nnnn ---m oooo oooo oooo		o = Opcode, u = Unknown, m = Enable Color, n = Unknown
		 *
		 * See @0C00657C, PH:@0C0CF882.
		 */
		{
			unsigned num = (inst[0] >> 16) & MATERIAL_MASK;
			unsigned unk = (inst[0] >> 12) & 1;

			/* XXX add offset-only mode */
			gpu->current_ms = &gpu->ms[num];

			VK_LOG ("GPU CMD %08X: Recall Material [%08X] num=%u ena=%u",
			        gpu->pc, inst[0], num, unk);

			gpu->pc += 4;
		}
		break;

	/* Texture Params
	 * ==============
	 *
	 * These instructions are thought to define the texture parameters
	 * for the vertex pushing commands. Their exact meaning is still
	 * unknown. */

	case 0x0C1:
		/* 0C1	Texhead: Set 0
		 *
		 *	---- mmmm mmmm nnnn ---- oooo oooo oooo	o = Opcode, n,m = Unknown
		 *
		 * See PH:@0C015B7A.
		 */
		gpu->ts_scratch._0C1_m = (inst[0] >> 20) & 0xFFFF;
		gpu->ts_scratch._0C1_n = (inst[0] >> 16) & 0xFF;

		VK_LOG ("GPU CMD %08X: Texhead: Set 0 [%08X] m=%u n=%u",
		        gpu->pc, inst[0],
		        gpu->ts_scratch._0C1_m,
		        gpu->ts_scratch._0C1_n);

		gpu->pc += 4;
		break;
	case 0x2C1:
		/* 2C1	Texhead: Set 2
		 *
		 *	8887 77ll ll66 6555 uu-- oooo oooo oooo
		 *
		 * 8 = argument on stack
		 * 7 = argument R7
		 * 6 = log16 of argument R6
		 * l = lower four bits of argument R4
		 * 5 = log16 of argument R5
		 * u = Upper two bits of argument R4
		 *
		 * XXX this guy is a mess.
		 *
		 * See PH:@0C015BCC
		 */
		VK_LOG ("GPU CMD %08X: Texhead: Set 2 [%08X]",
		        gpu->pc, inst[0]);

		gpu->pc += 4;
		break;
	case 0x4C1:
		/* 4C1	Texhead: Set 4
		 *
		 *	nnnn nnnn mmmm mmmm pppp oooo oooo oooo		o = Opcode, n, m, p = Unknown
		 *
		 * See PH:@0C015BA0.
		 */
		gpu->ts_scratch._4C1_n = inst[0] >> 24;
		gpu->ts_scratch._4C1_m = (inst[0] >> 16) & 0xFF;
		gpu->ts_scratch._4C1_p = (inst[0] >> 12) & 0xF;

		VK_LOG ("GPU CMD %08X: Texhead: Set 4 [%08X] n=%u m=%u p=%u",
		        gpu->pc, inst[0],
		        gpu->ts_scratch._4C1_n,
		        gpu->ts_scratch._4C1_m,
		        gpu->ts_scratch._4C1_p);

		gpu->pc += 4;
		break;
	case 0x0C4:
		/* 0C4	Commit Texhead
		 *
		 *	uuuu uuuu nnnn nnnn ---e oooo oooo oooo		o = Opcode, m = Unknown, n = Number
		 */
		{
			unsigned u = (inst[0] >> 24) & 0xFF;
			unsigned n = (inst[0] >> 16) & TEXHEAD_MASK;
			unsigned e = (inst[0] >> 12) & 1;

			gpu->ts[n] = gpu->ts_scratch;

			VK_LOG ("GPU CMD %08X: Commit Texhead [%08X] u=%u n=%u e=%u",
			        gpu->pc, inst[0], u, n, e);

			gpu->pc += 4;
		}
		break;
	case 0x0C3:
		/* 0C3	Recall Texhead
		 *
		 *	uuuu uuuu nnnn nnnn ---e oooo oooo oooo		o = Opcode, m = Enable Texturing, n = Number, u = Unknown
		 */
		{
			unsigned u = (inst[0] >> 24) & 0xFF;
			unsigned n = (inst[0] >> 16) & TEXHEAD_MASK;
			unsigned e = (inst[0] >> 12) & 1;

			/* XXX add offset-only mode */
			gpu->current_ts = &gpu->ts[n];

			VK_LOG ("GPU CMD %08X: Recall Texhead [%08X] u=%u n=%u e=%u",
			        gpu->pc, inst[0], u, n, e);

			gpu->pc += 4;
		}
		break;

	/* Lighting Operations
	 * ===================
	 *
	 * Just random notes, really.
	 */
	case 0x261:
	case 0x961:
	case 0xB61:
		/* 261	Set Light Vector
		 * 961	Set Light Vector
		 * B61	Set Light Vector
		 */
		{
			unsigned n = (inst[0] >> 12) & 7;
			unsigned m = (inst[0] >> 16) & 3;
			unsigned i = (inst[0] >> 18) & 3;
			vec3f_t *v = (vec3f_t *) &inst[1];

			VK_LOG ("GPU CMD %08X: Light: Vector [%08X %08X %08X %08X] %u %u %u <%f %f %f>",
			        gpu->pc,
			        inst[0], inst[1], inst[2], inst[3],
			        n, m, i,
			        v->x[0], v->x[1], v->x[2]);

			gpu->pc += 16;
		}
		break;
	case 0x051:
		/* 051	Light: Set Unknown
		 *
		 *	---- ---- nnnn nnnn ---- oooo oooo oooo	o = Opcode
		 *	--aa aaaa aaaa bbbb bbbb bbcc cccc cccc	a,b,c = Unknown
		 *
		 * See PH:@0C0178C6.
		 */
		{
			vec3s_t param;
			unsigned n;

			n = (inst[0] >> 16) & 0xFF;
			param.x[0] = inst[1] & 0x3FF;
			param.x[1] = (inst[1] >> 10) & 0x3FF;
			param.x[2] = (inst[1] >> 20) & 0x3FF;

			VK_LOG ("GPU CMD %08X: Light: Set Unknown [%08X %08X] num=%u param=<%u %u %u>",
			        gpu->pc, inst[0], inst[1],
			        n, param.x[2], param.x[1], param.x[0]);

			gpu->pc += 8;
		}
		break;
	case 0x451:
		/* 451	Light: Set Unknown
		 *
		 *	---- ---1 ---- ---- ---- oooo oooo oooo	o = Opcode, 1 = Unknown, always set
		 *	???? ???? ???? ???? ???? ???? ???? ????
		 *
		 * XXX I'm not sure this command is _two_ words long.
		 */
		VK_LOG ("GPU CMD %08X: Light: Set Unknown %03X [%08X %08X]",
		        gpu->pc, inst[0] & 0xFFF, inst[0], inst[1]);
		gpu->pc += 8;
		break;
	case 0x561:
		/* 561	Light: Set Unknown
		 *
		 *	---- ---- ---- --nn ---- oooo oooo oooo	o = Opcode
		 *	---- ---- ---- ---- ---- ---- ---- ----
		 *	---- ---- ---- ---- ---- ---- ---- ----
		 *	---- ---- ---- ---- ---- ---- ---- ----
		 */
		VK_LOG ("GPU CMD %08X: Light: Set Unknown %03X [%08X %08X %08X %08X]",
		        gpu->pc, inst[0] & 0xFFF, inst[0], inst[1], inst[2], inst[3]);
		gpu->pc += 16;
		break;
	case 0x043:
		/* 043	Recall Lightset
		 *
		 *	uuuu uuuu ---- mmmm nnnn oooo oooo oooo
		 */
		{
			unsigned u = (inst[0] >> 24) & 0xF;
			unsigned n = (inst[0] >> 12) & 0xF;
			VK_LOG ("GPU CMD %08X: Recall Unknown [%08X] n=%u u=%u",
			        gpu->pc, inst[0], n, u);
			gpu->pc += 4;
		}
		break;
	case 0x104:
		/* 104	Commit Lightset
		 *
		 *	---- ---- ---n nnnn ---- oooo oooo oooo	o = Opcode, n = Num
		 */
		{
			unsigned n = (inst[0] >> 16) & MATRIX_MASK;

			VK_LOG ("GPU CMD %08X: Commit Light [%08X] %u",
			        gpu->pc, inst[0], n);

			gpu->pc += 4;
		}
		break;
	case 0x064:
		/* 064  Light: Commit Unknown
		 *
		 *      ---- ---- ---- nnnn ---e oooo oooo oooo
		 *      bbbb bbbb bbbb bbbb aaaa aaaa aaaa aaaa
		 *      dddd dddd dddd dddd cccc cccc cccc cccc
		 *      ffff ffff ffff ffff ffff ffff ffff ffff
		 *
		 * n = Unknown
		 * e = Unknown
		 * a,b,c,d = indices of four light vectors
		 * f = floating point number
		 */
		{
			VK_LOG ("GPU CMD %08X: Light: Commit Unknown %03X [%08X %08X %08X %08X]",
			        gpu->pc, inst[0] & 0xFFF, inst[0], inst[1], inst[2], inst[3]);

			gpu->pc += 16;
		}
		break;
	case 0x006:
		/* 006	Light: Unknown
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo	o = Opcode
		 */
		VK_LOG ("GPU CMD %08X: Light: Unknown %03X [%08X]",
		        gpu->pc, inst[0] & 0xFFF, inst[0]);
		gpu->pc += 4;
		break;
	case 0x046:
		/* 046	Light: Unknown
		 *
		 *	---- ---- ---- ---n ---- oooo oooo oooo	o = Opcode, n = Unknown
		 */
		VK_LOG ("GPU CMD %08X: Light: Unknown %03X [%08X]",
		        gpu->pc, inst[0] & 0xFFF, inst[0]);
		gpu->pc += 4;
		break;


	/* Matrix Operations
	 * =================
	 *
	 * Details on these are fuzzy and incongruent at best. I'd expect
	 * a matrix stack somewhere, but it may as well be managed in
	 * software.
	 */

	case 0x161:
		/* 161	Set Matrix Vector
		 *
		 *	---- ---- ---- nnnn ---- oooo oooo oooo
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *	zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *
		 * n = Num
		 * x,y,z = Vector elements
		 *
		 * Note: bit 4 of n becomes bit 3 in PH:@0C015CF2. This is
		 * odd. It may highlight a 'set-offset' effect for matrix
		 * commands too.
		 *
		 * See @0C008080.
		 */
		{
			unsigned n = (inst[0] >> 12) & 7;
			unsigned m = (inst[0] >> 16) & 3;
			unsigned i = (inst[0] >> 18) & 3;
			vec3f_t *v = (vec3f_t *) &inst[1];

			VK_LOG ("GPU CMD %08X: Matrix: Vector [%08X %08X %08X %08X] %u %u %u <%f %f %f>",
			        gpu->pc,
			        inst[0], inst[1], inst[2], inst[3],
			        n, m, i,
			        v->x[0], v->x[1], v->x[2]);

			gpu->pc += 16;
		}
		break;

	/* Vertex Operations
	 * =================
	 *
	 * This class of instructions pushes (or otherwise deals with) vertex
	 * data to the hardware.
	 *
	 * All meshes seem to be defined in terms of tri strips; the exact
	 * connectivity pattern between different vertices, edge flags, and
	 * other parameters may be specified by the 'Unknown' fields.
	 *
	 * The main two actors here are the 'Vertex Normal' and the 'Vertex'
	 * commands. The former includes vertex metadata (texture coords,
	 * normals) for the given vertex, while the latter does not: in
	 * this case texture coords are supplied (for a whole triangle)
	 * by a following 'Set Tex Coords' command.
	 *
	 * Successive tri strips are demarked by the ??? command. XXX
	 */

	case 0x1B8:
	case 0x1BC:
	case 0x1BD:
	case 0xFB8:
	case 0xFBC:
	case 0xFBD:
	case 0xFBE:
	case 0xFBF:
		/* 1BC  Vertex Normal
		 *
		 *      pppp pppp mmmm nnnn qqqq oooo oooo oooo o = Opcode, n,m,p,q = Unknown
		 *      xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx x,y,z = Position
		 *      yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *      zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *      ssss ssss ssss ssss tttt tttt tttt tttt p,q = Tex Coords
		 *      uuuu uuuu uuuu uuuu uuuu uuuu uuuu uuuu u,v,w = Normal
		 *      vvvv vvvv vvvv vvvv vvvv vvvv vvvv vvvv
		 *      wwww wwww wwww wwww wwww wwww wwww wwww
		 */
		{
			unsigned n, m, p, q;
			vec3f_t *pos, *nrm;
			vec2s_t *texcoord;

			p = inst[0] >> 24;
			n = (inst[0] >> 20) & 15;
			m = (inst[0] >> 16) & 15;
			q = (inst[0] >> 12) & 15;

			pos = (vec3f_t *) &inst[1];
			nrm = (vec3f_t *) &inst[5];
			texcoord = (vec2s_t *) &inst[4];

			VK_LOG ("GPU CMD %08X: Vertex Normal [%08X %08X %08X %08X %08X %08X %08X %08X] <%f %f %f> <%f %f %f> <%X %X> %u %u %u %u",
			        gpu->pc,
				inst[0], inst[1], inst[2], inst[3],
				inst[4], inst[5], inst[6], inst[7],
			        pos->x[0], pos->x[1], pos->x[2],
			        nrm->x[0], nrm->x[1], nrm->x[2],
				texcoord->x[0], texcoord->x[1],
			        n, m, p, q);
			gpu->pc += 32;
		}
		break;
	case 0x12C:
	case 0x12D:
	case 0xF2C:
	case 0xF2D:
	case 0x1AC:
	case 0x1AD:
	case 0xFAC:
	case 0xFAD:
		/* xAC	Vertex 3f
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx		x = X coord
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy		y = Y coord
		 *	zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz		z = Z coord
		 *	*/
		{
			vec3f_t *v = (vec3f_t *) &inst[1];

			VK_LOG ("GPU CMD %08X: Vertex [%08X] { %f %f %f }",
			        gpu->pc, inst[0],
			        v->x[0], v->x[1], v->x[2]);

			append_vertex (gpu, v);
			gpu->pc += 16;
		}
		break;
	case 0xEE8:
	case 0xEE9:
		/* EE9	Tex Coord 3
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo	o = Opcode
		 *	???? yyyy yyyy yyyy ???? xxxx xxxx xxxx	y,x = Coords for Vertex 0
		 *	???? yyyy yyyy yyyy ???? xxxx xxxx xxxx	y,x = Coords for Vertex 1
		 *	???? yyyy yyyy yyyy ???? xxxx xxxx xxxx	y,x = Coords for Vertex 2
		 *
		 * Note: 12.4 fixed point?
		 */
		{
			vec2s_t uv[3];
			unsigned i;

			for (i = 0; i < 3; i++) {
				uv[i].x[0] = (inst[i+1] & 0xFFFF) >> 4;
				uv[i].x[1] = (inst[i+1] >> (16+5));
				uv[i].x[0] += 1920;
			}

			VK_LOG ("GPU CMD %08X: Tex Coord [%08X %08X %08X %08X] <%u %u> <%u %u> <%u %u>",
			        gpu->pc,
			        inst[0], inst[1], inst[2], inst[3],
			        uv[0].x[0], uv[0].x[1],
			        uv[1].x[0], uv[1].x[1],
			        uv[2].x[0], uv[2].x[1]);

			ASSERT (!(inst[1] & 0xF000F000));
			ASSERT (!(inst[2] & 0xF000F000));
			ASSERT (!(inst[3] & 0xF000F000));

			draw_tri (gpu, &uv[0], &uv[1], &uv[2]);
			gpu->pc += 16;
		}
		break;
#if 0
	case 0x158:
	case 0x159:
	case 0xF58:
	case 0xF59:
		/* 158	Unknown Vertex-related */
		{
			vec2s_t *unk = &inst[1];
			gpu->pc += 8;
		}
		break;
#endif

	/* Unknown */

	case 0x101:
		/* 101	Unknown [Begin Scene]
		 *
		 * A	---- --uu uuuu uuuu ---- oooo oooo oooo		o = Opcode, u = Unknown
		 * B	---- ---- ---- -1mm nnnn oooo oooo oooo		o = Opcode, n,m = Unknown, XXX not so sure about this
		 *
		 * See @0C008040, PH:@0C016418, PH:@0C016446 */
		{
			unsigned u = (inst[0] >> 24) & 1;
			VK_LOG ("GPU CMD %08X: Unknown: Set 101 [%08X] %u",
			        gpu->pc, inst[0], u);
			gpu->pc += 4;
		}
		break;
	case 0x301:
	case 0x501:
	case 0x901:
		/* x01	Unknown
		 *
		 *	---- ---- -nnn nnnn ---- oooo oooo oooo		o = Opcode, n = Unknown
		 */
		{
			unsigned n = (inst[0] >> 16) & 0x7F;
			VK_LOG ("GPU CMD %08X: Unknown: Set %03X [%08X] %u",
			        gpu->pc, inst[0] & 0xFFF, inst[0], n);
			gpu->pc += 4;
		}
		break;
	case 0x303:
	case 0x903:
	case 0xD03:
		/* x03	Light/Fog Related
		 *
		 *	FFFF FFFF ---- ---- ---- NNNN oooo oooo
		 *
		 * F = Fog-related value? See PH:@0C0DA8BC.
		 * N = Unknown; it can't be zero.
		 *
		 * See PH:@0C0173CA.
		 */
		{
			unsigned f = inst[0] >> 24;
			unsigned n = ((inst[0] >> 8) & 0xF) - 1;

			VK_LOG ("GPU CMD %08X: Recall %03X [%08X] f=%X n=%X",
			        gpu->pc, inst[0] & 0xFFF, inst[0], f, n);

			gpu->pc += 4;
		}
		break;
	case 0x313:
	case 0xD13:
		/* These could be just like the above; no idea. */
		{
			unsigned u = inst[0] >> 24;

			VK_LOG ("GPU CMD %08X: Recall %03X [%08X] %u",
			        gpu->pc, inst[0] & 0xFFF, inst[0], u);

			gpu->pc += 4;
		}
		break;

	/* More Unknown */

	case 0x3A1:
		/* 3A1	Set Lo Addresses; always comes in a pair with 5A1
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	llll llll llll llll llll llll llll llll
		 *	LLLL LLLL LLLL LLLL LLLL LLLL LLLL LLLL
		 *      0000 0000 0000 0000 0000 0000 0000 0000
		 *
		 * Perhaps set frame buffer? See PH:@0C016308 */
		{
			VK_LOG ("GPU CMD %08X: Set Lo Addresses [%08X %08X %08X %08X]",
			        gpu->pc, inst[0], inst[1], inst[2], inst[3]);
			ASSERT (!inst[3]);
			gpu->pc += 16;
		}
		break;
	case 0x5A1:
		/* 5A1	Set Hi Addresses; always comes in a pair with 3A1
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	uuuu uuuu uuuu uuuu uuuu uuuu uuuu uuuu
		 *	UUUU UUUU UUUU UUUU UUUU UUUU UUUU UUUU
		 *      0000 0000 0000 0000 0000 0000 0000 0000
		 *
		 * Perhaps set depth buffer? See PH:@0C016308 */
		{
			VK_LOG ("GPU CMD %08X: Set Hi Addresses [%08X %08X %08X %08X]",
			        gpu->pc, inst[0], inst[1], inst[2], inst[3]);
			ASSERT (!inst[3]);
			gpu->pc += 16;
		}
		break;
	case 0x6D1:
		/* 6D1	Unknown
		 *
		 *	---- ---- ---- --nn ---- oooo oooo oooo		o = Opcode
		 *	bbbb bbbb bbbb bbbb cccc cccc cccc cccc
		 *
		 * These come in quartets. May be related to matrices.
		 * See PH:@0C015C3E */
		{
			unsigned a = inst[0] >> 16;
			unsigned b = inst[1] & 0xFFFF;
			unsigned c = inst[1] >> 16;
			VK_LOG ("GPU CMD %08X: Unknown 6D1 [%08X %08X] <%u %u %u>",
			        gpu->pc, inst[0], inst[1], a, b, c);
			gpu->pc += 8;
		}
		break;
	case 0x181:
		/* 181	Unknown
		 *
		 *	---- ---b nnnn nnnn ---- oooo oooo oooo		o = Opcode, n = Unknown, b = set if n != 0
		 *
		 * See PH:@0C015B50 */
		{
			unsigned b = (inst[0] >> 24) & 1;
			unsigned n = (inst[0] >> 16) & 0xFF;
			VK_LOG ("GPU CMD %08X: Unknown 181 [%08X] <%u %u>",
			        gpu->pc, inst[0], b, n);
			gpu->pc += 4;
		}
		break;
	case 0xE88:
		/* E88	Unknown [Flush Vertices?] */
		{
			VK_LOG ("GPU CMD %08X: Unknown %03X [%08X]",
			        gpu->pc, inst[0] & 0xFFF, inst[0]);
			gpu->pc += 4;
		}
		break;
#if 0
	case 0x711:
		/* 711	Unknown
		 *
		 *	aaaa aaaa aaaa aaaa ---- oooo oooo oooo
		 *	bbbb bbbb bbbb bbbb cccc cccc cccc cccc
		 *
		 * See PH:@0C0162E2 */
		{
			unsigned a = inst[0] >> 16;
			unsigned b = inst[1] & 0xFFFF;
			unsigned c = inst[1] >> 16;
			gpu->pc += 8;
		}
		break;
#endif
	default:
		VK_ABORT ("GPU: @%08X: unhandled opcode %03X", gpu->pc, inst[0] & 0xFFF);
	}
	return 0;
}

static void
hikaru_gpu_begin_processing (hikaru_gpu_t *gpu)
{
	/* Check the GPU 15 execute bits */
	if (REG15 (0x58) == 3) {
		gpu->is_running = true;

		gpu->pc = REG15 (0x70);
		gpu->sp[0] = REG15 (0x74);
		gpu->sp[1] = REG15 (0x78);

		memset (gpu->vertex_buffer, 0, 3 * sizeof (vec3f_t));
		gpu->vertex_index = 0;
	} else
		REG1A (0x24) = 0;
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
 * GPU Indirect DMA
 * ================
 *
 * Register 1500000C points to a table in GPU CMDRAM, defaulting to
 * 483FC000. Each entry has this format:
 *	
 *	3FC000: 48300000	Source address
 *	3FC004: 00002000	Lenght (in bytes)
 *	3FC008: 0812C080	Unknown (bitfield)
 *	3FC00C: 00000000	Unknown	(byte)
 *
 * Data can be located (at least) at 48xxxxxx (CMDRAM) or at 41xxxxxx
 * (slave RAM).
 *
 * During the bootrom life-cycle, the data address to texture-like data (the
 * not-yet-converted ASCII texture.) However, the bootrom uploads this
 * texture independently to TEXRAM by performing the format conversion
 * manually (RGBA1 to RGBA4); it does however use the GPU IDMA mechanism
 * too. I don't know why.
 *
 * The third and fourth parameters decide the type of operation to do. Their
 * format is still unknown.
 *
 * Note: C080 and x812 are also used as parameters for the `Set X' GPU
 * command. No idea if there is any relation. Possibly texture format?
 *
 * Note: GPU 15 IDMA fires GPU 15 IRQ 1 when done.
 */

static void
hikaru_gpu_step_idma (hikaru_gpu_t *gpu)
{
	/* Step the GPU 15 indirect DMA thing */
	uint32_t entry[4], addr;

	if (!(REG15 (0x14) & 1) || !REG15 (0x10))
		return;

	VK_ASSERT ((REG15 (0x0C) >> 24) == 0x48);

	/* Read the IDMA table address in CMDRAM */
	addr = (REG15 (0x0C) & 0xFFFFFF);

	entry[0] = vk_buffer_get (gpu->cmdram, 4, addr+0x0);
	entry[1] = vk_buffer_get (gpu->cmdram, 4, addr+0x4);
	entry[2] = vk_buffer_get (gpu->cmdram, 4, addr+0x8);
	entry[3] = vk_buffer_get (gpu->cmdram, 4, addr+0xC);

	VK_LOG (" ## GPU 15 IDMA entry = [ %08X %08x %08X %08X <%u %u %X> ]",
	        entry[0], entry[1], entry[2], entry[3],
	        entry[2] & 0xFF, (entry[2] >> 8) & 0xFF, entry[2] >> 16);

	/* If the entry supplies a positive lenght, process it */
	if (entry[1]) {
		/* XXX actually process it ... */
		REG15 (0x0C) += 0x10;
		REG15 (0x10) --;
	}

	/* XXX note that the bootrom code assumes that the IDMA may stop even
	 * if there are still unprocessed entries. This probably means that
	 * the IDMA somehow stops processing when any other GPU IRQ fires */

	VK_LOG (" ### GPU 15 IDMA status became = [ %08X %08X %08X ]",
	        REG15 (0x0C), REG15 (0x10), REG15 (0x14));

	/* If there are no more entries, stop */
	if (REG15 (0x10) == 0) {
		/* XXX I don't think it actually gets overwritten considering
		 * that the IRL2 handler does it itself */
		REG15 (0x14) = 0;
		hikaru_gpu_raise_irq (gpu, _15_IRQ_IDMA, 0);
	}
}

/*
 * FIFO at 1A040000
 * ================
 *
 * Copies texture data from TEXRAM to the framebuffer(s)
 *
 * See AT:@0C697D48, PH:@0C0CD320.
 *
 * 1A040000  32-bit  W	Source
 * 1A040004  32-bit  W  Destination
 * 1A040008  32-bit  W	Texture size in pixels.
 * 1A04000C  32-bit  W	Control
 *
 * Both source and destination are encoded as TEXRAM coordinates; both
 * x and y are defined as 11-bit integers (range is 0 ... 2047); pixel
 * size is 16-bit, fixed.
 *
 * 1A000024 bit 0 signals when the FIFO is processing: set means busy.
 * The AIRTRIX 'WARNING' screen uses this thing to raster text on the
 * framebuffer.
 *
 * Texture RAM
 * ===========
 *
 * Located at 1B000000-1B7FFFFF in the master SH-4 address space, 8MB large;
 * it is a single (double?) sheet of texel data. Supported texture formats
 * include RGBA4444, RGB565, RGBA5551, RGBA8888.
 *
 * It looks like the sheet has an (1 << 11) = 8192 bytes pitch.  See
 * PH:@0C01A242.
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
	hikaru_renderer_t *hr = (hikaru_renderer_t *) gpu->base.mach->renderer;
	unsigned i, j, offs;
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
#if 0
			        VK_LOG ("LAYER %u: [%08X-%08X] { %u, %u } - { %u, %u }",
			                j, lo, hi,
				        rect[0].x[0], rect[1].x[0],
				        rect[0].x[1], rect[1].x[1]);
#endif
				hikaru_renderer_draw_layer (hr, rect);
			}
		}
}

static void
hikaru_gpu_begin_fifo_operation (hikaru_gpu_t *gpu)
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

/* V-blanking events */

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

/* Main loop */

static int
hikaru_gpu_exec (vk_device_t *dev, int cycles)
{
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) dev;

	/* Step the GPU 15 indirect DMA thing */
	hikaru_gpu_step_idma (gpu);

	/* Step the GPU 1A texture FIFO thing */
	/* TODO */

	if (!gpu->is_running || REG15 (0x58) != 3)
		return 0;

	/* XXX hack, no idea how fast the GPU is or how much time each
	 * command takes. */
	gpu->cycles = cycles;
	while (gpu->cycles > 0) {
		if (hikaru_gpu_exec_one (gpu)) {
			hikaru_gpu_end_processing (gpu);
			gpu->cycles = 0;
			break;
		}
		gpu->cycles--;
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
			hikaru_gpu_begin_processing (gpu);
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
			break;
		case 0x08: /* GPU 1A IRQ 0 */
		case 0x0C: /* GPU 1A IRQ 1 */
		case 0x10: /* GPU 1A IRQ 2 */
		case 0x14: /* GPU 1A IRQ 3 */
			/* Bit 0 is ANDNOT'ed on write; I have no clue about
			 * the other bits. */
			VK_ASSERT (val == 1);
			REG1A(addr) &= ~val;
			hikaru_gpu_update_irqs (gpu);
			return 0;
		case 0x24:
			REG1A (addr) = val;
			hikaru_gpu_begin_processing (gpu);
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
			hikaru_gpu_begin_fifo_operation (gpu);
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
hikaru_gpu_delete (vk_device_t **_dev)
{
	FREE (_dev);
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
