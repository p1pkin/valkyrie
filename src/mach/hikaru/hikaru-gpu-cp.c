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

/*
 * GPU CP
 * ======
 *
 * The GPU CP is the Command Processor that performs 3D drawing.
 *
 *
 * CP Execution
 * ============
 *
 * CP execution is likely initiated by writing 15000058=3 and 1A000024=1, but
 * may actually begin only on the next vblank-in event.
 *
 * (From a software perspective, the CP program is uploaded when both IRQs 2 of
 * GPU 15 and GPU 1A are fired, meaning that at this point the CP is supposed
 * to have consumed the previous command stream and is ready to accept a new
 * one.)
 *
 * Note also that a CP program is uploaded to two different areas in CMDRAM on
 * odd and even frames. This may mean that there are *two* CPs performing
 * double-buffered 3D rendering.
 *
 * When execution ends:
 *
 *  - 1A00000C bit 0 is set (not sure)
 *    - 1A000018 bit 1 is set as a consequence
 *      - 15000088 bit 7 is set as a consequence
 *  - 15000088 bit 1 is set; a GPU IRQ is raised (if not masked by 15000084)
 *  - 15002000 bit 0 is set on some HW revisions (not sure)
 *  - 1A000024 bit 0 is cleared if some additional condition occurred (not sure)
 *
 * 15002000 and 1A000024 signal different things; see the termination condition
 * in sync().
 *
 * (Guesstimate: 15000088 bit 1 is set when the CP ends verifying/processing
 * the CS (if there is such a thing!); 15002000 is cleared when the CP submits
 * the completely rasterized frame buffer to the GPU 1A; 1A000024 is cleared
 * when the GPU 1A is done compositing the frame buffer with the 2D layers and
 * has displayed them on-screen.)
 */

static void
on_frame_begin (hikaru_gpu_t *gpu)
{
	if (gpu->debug.log_cp)
		VK_LOG (" ==== CLEARING CP DATA ==== ");

	gpu->state.in_mesh = 0;

	memset ((void *) &POLY, 0, sizeof (POLY));

	LOD.value = 0.0f;
	LOD.cond = false;
	LOD.branch_id = ~0;

	VP.depth = 0;
	VP.scratch.flags = 0;
	VP.scratch.uploaded = 1;
	VP.scratch.dirty = 1;

	MV.depth = 0;
	MV.total = 0;

	MAT.base = 0;
	MAT.scratch.flags = 0;
	MAT.scratch.uploaded = 1;
	MAT.scratch.dirty = 1;

	TEX.base = 0;
	TEX.scratch.flags = 0;
	TEX.scratch.uploaded = 1;
	TEX.scratch.dirty = 1;

	LIT.base = 0;
	LIT.scratch.flags = 0;
	LIT.scratch.uploaded = 1;
	LIT.scratchset.flags = 0;
	LIT.scratchset.uploaded = 1;
	LIT.scratchset.dirty = 1;
}

static void
on_cp_begin (hikaru_gpu_t *gpu)
{
	if (gpu->debug.log_cp)
		VK_LOG (" ==== CP BEGIN ==== ");

	gpu->cp.is_running = true;

	PC = REG15 (0x70);
	SP(0) = REG15 (0x74);
	SP(1) = REG15 (0x78);
}

static void
on_cp_end (hikaru_gpu_t *gpu)
{
	if (gpu->debug.log_cp)
		VK_LOG (" ==== CP END ==== ");

	/* Turn off the busy bits */
	REG15 (0x58) &= ~3;
	REG1A (0x24) &= ~1;

	/* Notify that GPU 15 is done and needs feeding */
	hikaru_gpu_raise_irq (gpu, _15_IRQ_DONE, _1A_IRQ_DONE);
}

void
hikaru_gpu_cp_vblank_in (hikaru_gpu_t *gpu)
{
	on_frame_begin (gpu);
}

void
hikaru_gpu_cp_vblank_out (hikaru_gpu_t *gpu)
{
	/* no-op */
}

void
hikaru_gpu_cp_on_put (hikaru_gpu_t *gpu)
{
	/* Check the GPU 15 execute bits */
	if (REG15 (0x58) == 3)
		on_cp_begin (gpu);
	else
		REG1A (0x24) = 0; /* XXX really? */
}

/*
 * CP Objects
 * ==========
 *
 * The CP manipulates six kinds of objects: viewports, modelviews, materials,
 * textures/texheads (that's what they are called in PHARRIER, and here),
 * lights/lightsets (a lightset is a set of four lights), and meshes.
 *
 * The CP has a table for each object type, except meshes, namely:
 *
 *  viewports	8
 *  modelviews  < 256
 *  materials	16384 total? (can lookup at distance 0-255 from the base)
 *  texheads	16384 total? (can lookup at distance 0-255 from the base)
 *  lights	1024
 *  lightsets	256
 *
 * CP instructions do not manipulate the objects in the tables directly.
 * Instead, they work on a special object, the "scratch" or "active" object:
 * "recall" instructions load an object from the object table into the scratch
 * object, "set" instructions set the properties of the scratch objects, and
 * "commit" instructions store the scratch object back into the table.
 *
 * When a mesh is drawn, the current scratch objects affect its rendering,
 * e.g., the scratch material determines the mesh colors, shininess, etc.
 *
 *
 * CP Instructions
 * ===============
 *
 * Each GPU instruction is made of 1, 2, 4, or 8 32-bit words. The opcode is
 * specified by the lower 9 bits of the first word. The instruction size is
 * stored in bits 4-5 of the first word.
 */

#define HR        gpu->renderer

#define FLAG_JUMP	(1 << 0)
#define FLAG_BEGIN	(1 << 1)
#define FLAG_CONTINUE	(1 << 2)
#define FLAG_PUSH	(FLAG_BEGIN | FLAG_CONTINUE)
#define FLAG_STATIC	(1 << 3)
#define FLAG_INVALID	(1 << 4)

static struct {
	void (* handler)(hikaru_gpu_t *, uint32_t *);
	uint32_t flags;
} insns[0x200];

void (* disasm[0x200])(hikaru_gpu_t *, uint32_t *);

static uint32_t
get_insn_size (uint32_t *inst)
{
	return 1 << (((inst[0] >> 4) & 3) + 2);
}

static void
print_disasm (hikaru_gpu_t *gpu, uint32_t *inst, const char *fmt, ...)
{
	char out[256], *tmp = out;
	uint32_t nwords;
	va_list args;
	unsigned i;

	nwords = get_insn_size (inst) / 4;

	VK_ASSERT (gpu);
	VK_ASSERT (inst);
	VK_ASSERT (nwords <= 8);

	if (!gpu->debug.log_cp)
		return;

	va_start (args, fmt);
	tmp += sprintf (tmp, "CP @%08X : ", PC);
	for (i = 0; i < 8; i++) {
		if (i < nwords)
			tmp += sprintf (tmp, "%08X ", inst[i]);
		else
			tmp += sprintf (tmp, "........ ");
	}
	tmp += sprintf (tmp, "%c %c ",
	                UNHANDLED ? '!' : ' ',
	                gpu->state.in_mesh ? 'M' : ' ');
	vsnprintf (tmp, sizeof (out), fmt, args);
	va_end (args);

	VK_LOG ("%s", out);
}

#define DISASM(fmt_, args_...) \
	print_disasm (gpu, inst, fmt_, ##args_)

static void
check_self_loop (hikaru_gpu_t *gpu, uint32_t target)
{
	/* XXX at some point we'll need something better than this */
	if (target == PC) {
		VK_ERROR ("CP: @%08X: self-jump, terminating", target);
		gpu->cp.is_running = false;
	}
}

static void
push_pc (hikaru_gpu_t *gpu)
{
	VK_ASSERT ((SP(0) >> 24) == 0x48);
	vk_buffer_put (gpu->cmdram, 4, SP(0) & 0x3FFFFFF, PC);
	SP(0) -= 4;
}

static void
pop_pc (hikaru_gpu_t *gpu)
{
	SP(0) += 4;
	VK_ASSERT ((SP(0) >> 24) == 0x48);
	PC = vk_buffer_get (gpu->cmdram, 4, SP(0) & 0x3FFFFFF) + 8;
}

static int
fetch (hikaru_gpu_t *gpu, uint32_t **inst)
{
	hikaru_t *hikaru = (hikaru_t *) gpu->base.mach;

	/* The CP program has been observed to lie only in CMDRAM and slave
	 * RAM so far. */
	switch (PC >> 24) {
	case 0x40:
	case 0x41:
		*inst = (uint32_t *) vk_buffer_get_ptr (hikaru->ram_s,
		                                        PC & 0x01FFFFFF);
		return 0;
	case 0x48:
	case 0x4C: /* XXX not sure */
		*inst = (uint32_t *) vk_buffer_get_ptr (hikaru->cmdram,
		                                        PC & 0x003FFFFF);
		return 0;
	}
	return -1;
}

void
hikaru_gpu_cp_exec (hikaru_gpu_t *gpu, int cycles)
{
	if (!gpu->cp.is_running)
		return;

	while (cycles > 0 && gpu->cp.is_running) {
		uint32_t *inst, op;
		uint16_t flags;

		if (fetch (gpu, &inst)) {
			VK_ERROR ("CP %08X: invalid PC, skipping CS", PC);
			gpu->cp.is_running = false;
			break;
		}

		op = inst[0] & 0x1FF;

		flags = insns[op].flags;
		if (flags & FLAG_INVALID) {
			VK_LOG ("CP @%08X: invalid instruction [%08X]", PC, *inst);
			gpu->cp.is_running = false;
			break;
		}

		if (!gpu->state.in_mesh && (flags & FLAG_BEGIN)) {
			bool is_static = (flags & FLAG_STATIC) != 0;
			hikaru_renderer_begin_mesh (HR, PC, is_static);
			gpu->state.in_mesh = 1;
		} else if (gpu->state.in_mesh && !(flags & FLAG_CONTINUE)) {
			hikaru_renderer_end_mesh (HR, PC);
			gpu->state.in_mesh = 0;
		}

		if (gpu->debug.log_cp) {
			UNHANDLED = 0;
			disasm[op] (gpu, inst);
			if (UNHANDLED)
				VK_ERROR ("CP @%08X : unhandled instruction", PC);
		}

		insns[op].handler (gpu, inst);

		if (!(flags & FLAG_JUMP))
			PC += get_insn_size (inst);

		cycles--;
	}

	if (!gpu->cp.is_running)
		on_cp_end (gpu);
}

#define I(name_) \
	static void hikaru_gpu_inst_##name_ (hikaru_gpu_t *gpu, uint32_t *inst)

#define D(name_) \
	static void hikaru_gpu_disasm_##name_ (hikaru_gpu_t *gpu, uint32_t *inst)

/*
 * Control Flow
 * ============
 *
 * The CP supports jumps and subroutine calls, including conditional calls
 * (for selecting the right mesh LOD probably?)
 *
 * The call stack is probably held in CMDRAM at the addresses specified by
 * MMIOs 1500007{4,8}.
 */

static uint32_t
get_jump_address (hikaru_gpu_t *gpu, uint32_t *inst)
{
	uint32_t addr;

	addr = inst[1] * 4;
	if (inst[0] & 0x800)
		addr += PC;

	return addr;
}

/* 000	Nop
 *
 *	-------- -------- -------o oooooooo
 */

I (0x000)
{
}

D (0x000)
{
	DISASM ("nop");

	UNHANDLED |= (inst[0] != 0);
}

/* 012	Jump
 *
 *	IIIIIIII IIIIIIII CCCCR--o oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * I = Branch identifier?
 * C = Condition
 * R = Relative
 * A = Address or Offset in 32-bit words.
 */

I (0x012)
{
	uint32_t addr = get_jump_address (gpu, inst);
	uint32_t branch_id = inst[0] >> 16;
	bool jump;

	switch ((inst[0] >> 12) & 0xF) {
	case 0x0:
		jump = true;
		break;
	case 0x1:
		jump = LOD.branch_id != branch_id;
		break;
	case 0x5:
		/* XXX draws high-poly player character in PHARRIER; has
		 * branch_id != 0. */
		jump = LOD.cond == true;
		break;
	case 0x6:
		jump = LOD.cond == false;
		break;
	case 0x7:
		jump = LOD.cond == true;
		break;
	case 0x9:
		/* XXX draws low-poly player character in PHARRIER; has
		 * branch_id != 0. */
		jump = LOD.cond == false;
		break;
	case 0xD:
		jump = LOD.branch_id == branch_id;
		break;
	default:
		jump = false;
		break;
	}

	check_self_loop (gpu, addr);
	if (jump)
		PC = addr;
	else
		PC += 8;
}

D (0x012)
{
	static const char *cond[16] = {
		"",		"NEQ BID",	"?2?",		"?3?",
		"?4",		"COND",		"!COND",	"COND",
		"?8?",		"!COND",	"?A?",		"?B?",
		"?C?",		"EQ BID",	"?E?",		"?F?"
	};
	uint32_t addr = get_jump_address (gpu, inst);

	UNHANDLED |= !!(inst[0] & 0x00000600);

	DISASM ("jump %s @%08X [BID=%04X]",
	        cond[(inst[0] >> 12) & 0xF], addr, inst[0] >> 16);
}

/* 052	Call
 *
 *	-------- -------- CCCCR--o oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * C = Condition
 * R = Relative
 * A = Address or Offset in 32-bit words.
 */

I (0x052)
{
	uint32_t addr = get_jump_address (gpu, inst);
	bool jump;

	switch ((inst[0] >> 12) & 0xF) {
	case 0x0:
		jump = true;
		break;
	case 0x4:
		jump = LOD.cond == false;
		break;
	case 0x8:
		jump = LOD.cond == true;
		break;
	default:
		jump = false;
		break;
	}

	check_self_loop (gpu, addr);
	if (jump) {
		push_pc (gpu);
		PC = addr;
	} else
		PC += 8;
}

D (0x052)
{
	static const char *cond[16] = {
		"",		"?1?",		"?2?",		"?3?",
		"!COND",	"?5?",		"?6?",		"?7?",
		"COND",		"?9?",		"?A?",		"?B?",
		"?C?",		"?D?",		"?E?",		"?F?",
	};
	uint32_t addr = get_jump_address (gpu, inst);

	UNHANDLED |= !!(inst[0] & 0xFFFF3600);

	DISASM ("call %s @%08X", cond[(inst[0] >> 12) & 0xF], addr);
}

/* 082	Return
 *
 *	-------- -------- CCCC---o oooooooo
 *
 * C = Condition
 */

I (0x082)
{
	bool jump;

	switch ((inst[0] >> 12) & 0xF) {
	case 0x0:
		jump = true;
		break;
	case 0x4:
		jump = LOD.cond == false;
		break;
	case 0x8:
		jump = LOD.cond == true;
		break;
	default:
		jump = false;
		break;
	}

	if (jump)
		pop_pc (gpu);
	else
		PC += 4;
}

D (0x082)
{
	static const char *cond[16] = {
		"",		"?1?",		"?2?",		"?3?",
		"!COND",	"?5?",		"?6?",		"?7?",
		"COND",		"?9?",		"?A?",		"?B?",
		"?C?",		"?D?",		"?E?",		"?F?",
	};
	UNHANDLED |= !!(inst[0] & 0xFFFF3E00);

	DISASM ("ret %s", cond[(inst[0] >> 12) & 0xF]);
}

/* 1C2	Kill
 *
 *	-------- -------- -------o oooooooo
 */

I (0x1C2)
{
	gpu->cp.is_running = false;
}

D (0x1C2)
{
	UNHANDLED |= !!(inst[0] & 0xFFFFFE00);

	DISASM ("kill");
}

/* 005	LOD: Set Threshold Lower-Bound
 *
 *	TTTTTTTT TTTTTTTT TTTT---o oooooooo
 *
 * T = Truncated floating-point threshold.
 *
 *	Since the threshold is always positive, truncating the lower 12 bits
 *	makes the resulting value smaller = a lower bound.
 *
 * Used in conjunction with conditional control-flow. See SGNASCAR.
 */

I (0x005)
{
	alias32uf_t thresh;

	thresh.u = inst[0] & 0xFFFFF000;

	LOD.cond = LOD.value < (thresh.f * 8.0f);
}

D (0x005)
{
	alias32uf_t thresh;

	thresh.u = inst[0] & 0xFFFFF000;

	UNHANDLED |= !!(inst[0] & 0x00000E00);

	DISASM ("lod: set threshold lb [%f]", thresh.f);
}

/* 055	LOD: Set Threshold
 *
 *	-------- -------- -------o oooooooo
 *	TTTTTTTT TTTTTTTT TTTTTTTT TTTTTTTT
 *
 * T = Floating-point threshold
 */

I (0x055)
{
	float thresh = *(float *) &inst[1];

	LOD.cond = LOD.value < (thresh * 4.0f);
}

D (0x055)
{
	UNHANDLED |= !!(inst[0] & 0xFFFFFE00);

	DISASM ("lod: set threshold [%f]", *(float *) &inst[1]);
}

/* 095	LOD: Set Branch IDs
 *
 *	-------- -------- CCCC---o oooooooo
 *	HHHHHHHH HHHHHHHH LLLLLLLL LLLLLLLL
 *
 * C = Condition
 * H, L = Branch IDs
 */

I (0x095)
{
	uint16_t hi = inst[1] >> 16;
	uint16_t lo = inst[1] & 0xFFFF;

	switch ((inst[0] >> 12) & 0xF) {
	case 0x4:
		LOD.branch_id = LOD.cond ? lo : hi;
		break;
	case 0x8:
		LOD.branch_id = LOD.cond ? hi : lo;
		break;
	default:
		LOD.branch_id = ~0;
		break;
	}
}

D (0x095)
{
	UNHANDLED |= !!(inst[0] & 0xFFFF3E00);

	DISASM ("lod: set branch ids [%04X %04X]",
	        inst[1] >> 16, inst[1] & 0xFFFF);
}

/*
 * Viewports
 * =========
 *
 * These specify an on-screen rectangle (a subregion of the framebuffer,
 * presumably), a projection matrix, the depth-buffer and depth queue
 * configuration, ambient lighting and clear color. The exact meaning of the
 * various fields are still partially unknown.
 */

static uint32_t
get_viewport_index (uint32_t *inst)
{
	return (inst[0] >> 16) & (NUM_VIEWPORTS - 1);
}

static float
decode_clip_xy (uint32_t c)
{
	return (float) ((((int32_t) (int16_t) c) << 3) >> 3);
}

/* 021	Viewport: Set Z Clipping
 *
 *	-------- -------- -------o oooooooo
 *	FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF
 *	ffffffff ffffffff ffffffff ffffffff
 *	NNNNNNNN NNNNNNNN NNNNNNNN NNNNNNNN
 *
 * F = Far clipping plane
 * f = Alt. far clipping plane (see SGNASCAR)
 * N = Near clipping plane
 *
 * See PH:@0C01587C, PH:@0C0158A4, PH:@0C0158E8.
 *
 *
 * 221	Viewport: Set XY Clipping and Offset
 *
 *	-------- -------- -------o oooooooo
 *	YYYYYYYY YYYYYYYY XXXXXXXX XXXXXXXX
 *	--BBBBBB BBBBBBBB -LLLLLLL LLLLLLLL
 *	--TTTTTT TTTTTTTT -RRRRRRR RRRRRRRR
 *
 * T, B, L, R = Clipping planes
 * X, Y = Viewport offset
 *
 * See PH:@0C015924
 *
 *
 * 421	Viewport: Set Depth Range
 *
 *	-------- -------- -------o oooooooo
 *	mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm
 *	MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM
 *	FFF----- -------- -------- --------
 *
 * m = Minimum
 * M = Maximum
 * F = Depth test function
 *
 * See PH:@0C015AA6
 *
 *
 * 621	Viewport: Set Depth Queue
 *
 *	-------- ---TT-DU -------o oooooooo
 *	AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
 *	PPPPPPPP PPPPPPPP PPPPPPPP PPPPPPPP
 *	QQQQQQQQ QQQQQQQQ QQQQQQQQ QQQQQQQQ
 *
 * T = Depth queue type
 * D = Disable (R, G, B, A are ignored)
 * U = Unknown
 * R, G, B, A = Color
 * P = Depth queue density
 *
 *	if T = 0, P = (1 / |depth queue end - depth queue start|), else it
 *	is P = |depth queue density|
 *
 * Q = near / depth queue start
 *
 * See PH:@0C0159C4, PH:@0C015A02, PH:@0C015A3E.
 */

I (0x021)
{
	hikaru_viewport_t *vp = &VP.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		vp->clip.f = *(float *) &inst[1];
		vp->clip.f2 = *(float *) &inst[2];
		vp->clip.n = *(float *) &inst[3];
		vp->has_021 = 1;
		break;
	case 2:
		vp->offset.x = (float) (inst[1] & 0xFFFF);
		vp->offset.y = (float) (inst[1] >> 16);
		vp->clip.l = decode_clip_xy (inst[2]);
		vp->clip.r = decode_clip_xy (inst[3]);
		vp->clip.b = decode_clip_xy (inst[2] >> 16);
		vp->clip.t = decode_clip_xy (inst[3] >> 16);
		vp->has_221 = 1;
		break;
	case 4:
		vp->depth.min = *(float *) &inst[1];
		vp->depth.max = *(float *) &inst[2];
		vp->depth.func = inst[3] >> 29;
		vp->has_421 = 1;
		break;
	case 6:
		vp->depth.q_type	= (inst[0] >> 18) & 3;
		vp->depth.q_enabled	= ((inst[0] >> 17) & 1) ^ 1;
		vp->depth.q_unknown	= (inst[0] >> 16) & 1;
		vp->depth.mask[0]	= inst[1] & 0xFF;
		vp->depth.mask[1]	= (inst[1] >> 8) & 0xFF;
		vp->depth.mask[2]	= (inst[1] >> 16) & 0xFF;
		vp->depth.mask[3]	= inst[1] >> 24;
		vp->depth.density	= *(float *) &inst[2];
		vp->depth.bias		= *(float *) &inst[3];
		vp->has_621 = 1;
		break;
	default:
		VK_ASSERT (0);
		break;
	}
	vp->uploaded = 1;
}

D (0x021)
{
	switch ((inst[0] >> 8) & 7) {
	case 0:
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);

		DISASM ("vp: set clip Z [f=%f f2=%f n=%f]",
		        *(float *) &inst[1], *(float *) &inst[2], *(float *) &inst[3]);
		break;

	case 2:
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
	
		DISASM ("vp: set clip XY [clipxy=(%f %f %f %f) offs=(%f,%f)]",
		        decode_clip_xy (inst[2]), decode_clip_xy (inst[3]),
		        decode_clip_xy (inst[2] >> 16), decode_clip_xy (inst[3] >> 16),
		        (float) (inst[1] & 0xFFFF), (float) (inst[1] >> 16));
		break;

	case 4:
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
		UNHANDLED |= !!(inst[3] & 0x1FFFFFFF);

		DISASM ("vp: set depth [func=%u range=(%f,%f)]",
		        inst[3] >> 29, *(float *) &inst[1], *(float *) &inst[2]);
		break;

	case 6:
		UNHANDLED |= !!(inst[0] & 0xFFF0F800);
	
		DISASM ("vp: set depth queue [type=%u ena=%u unk=%u mask=(%08X) density=%f bias=%f]",
		        (inst[0] >> 18) & 3, ((inst[0] >> 17) & 1) ^ 1,
		        (inst[0] >> 16) & 1, inst[1],
		        *(float *) &inst[2], *(float *) &inst[3]);
		break;
	}
}

/* 011	Viewport: Set Ambient Color
 *
 *	rrrrrrrr rrrrrrrr ----1--o oooooooo
 *	bbbbbbbb bbbbbbbb gggggggg gggggggg
 *
 * r,g,b = color
 *
 * See PH:@0C037840.
 */

I (0x011)
{
	hikaru_viewport_t *vp = &VP.scratch;

	vp->color.ambient[0] = inst[0] >> 16;
	vp->color.ambient[1] = inst[1] & 0xFFFF;
	vp->color.ambient[2] = inst[1] >> 16;

	vp->has_011 = 1;
	vp->uploaded = 1;
}

D (0x011)
{
	UNHANDLED |= !!(inst[0] & 0x0000F600);
	UNHANDLED |= !(inst[0] & 0x00000800);

	DISASM ("vp: set ambient [%X %X %X]",
	        inst[0] >> 16, inst[1] & 0xFFFF, inst[1] >> 16);
}

/* 191	Viewport: Set Clear Color
 *
 *	-------- -------- ----1--o oooooooo
 *	-------a gggggggg bbbbbbbb rrrrrrrr
 *
 * a,r,g,b = color.
 *
 * NOTE: yes, apparently blue and green _are_ swapped.
 *
 * XXX double check the alpha mask.
 *
 * See PH:@0C016368, PH:@0C016396, PH:@0C037760.
 */

I (0x191)
{
	hikaru_viewport_t *vp = &VP.scratch;

	vp->color.clear[0] = inst[1] & 0xFF;
	vp->color.clear[1] = (inst[1] >> 8) & 0xFF;
	vp->color.clear[2] = (inst[1] >> 16) & 0xFF;
	vp->color.clear[3] = ((inst[1] >> 24) & 1) ? 0xFF : 0;

	vp->has_191 = 1;
	vp->uploaded = 1;
}

D (0x191)
{
	UNHANDLED |= !!(inst[0] & 0xFFFFF600);
	UNHANDLED |= !(inst[0] & 0x00000800);
	UNHANDLED |= !!(inst[0] & 0xFE000000);

	DISASM ("vp: set clear [%X]", inst[1]);
}

/* 004	Commit Viewport
 *
 *	-------- -----iii -------o oooooooo
 *
 * i = Index
 *
 * See PH:@0C015AD0.
 */

I (0x004)
{
	hikaru_viewport_t *vp = &VP.table[get_viewport_index (inst)];

	*vp = VP.scratch;
}

D (0x004)
{
	UNHANDLED |= !!(inst[0] & 0xFFF8FE00);

	DISASM ("vp: commit @%u", get_viewport_index (inst));
}

/* 003	Recall Viewport
 *
 *	-------- -----iii -pP----o oooooooo
 *
 * i = Index
 *
 * P = Push
 *
 *	Pushes current viewport on the stack and uses the one specified by i.
 *
 * p = Pop
 *
 *	Pops the viewport on the stack and uses it.
 *
 * Information kindly provided by DreamZzz.
 *
 * See PH:@0C015AF6, PH:@0C015B12, PH:@0C015B32.
 */

I (0x003)
{
	hikaru_viewport_t *vp = &VP.scratch;

	switch ((inst[0] >> 12) & 0xF) {
	case 0:
		*vp = VP.table[get_viewport_index (inst)];
		break;
	case 2:
		VP.stack[VP.depth] = *vp;
		VP.depth++;
		VK_ASSERT (VP.depth < 32);

		*vp = VP.table[get_viewport_index (inst)];
		break;
	case 4:
		VP.depth--;
		VK_ASSERT (VP.depth >= 0);
		*vp = VP.stack[VP.depth];
		break;
	default:
		VK_ASSERT (0);
		break;
	}
	vp->uploaded = 1;
}

D (0x003)
{
	char *op;

	switch ((inst[0] >> 12) & 0xF) {
	case 0:
		op = "";
		break;
	case 2:
		op = "push";
		break;
	case 4:
		op = "pop";
		break;
	default:
		op = "unknown";
		UNHANDLED = 1;
		break;
	}

	UNHANDLED |= !!(inst[0] & 0xFFF89E00);

	DISASM ("vp: recall @%u %s", get_viewport_index (inst), op);
}

/*
 * Modelview Matrix
 * ================
 *
 * The CP uses command 161 to upload each (row) vector of the modelview matrix
 * separately.  The CP can also perform instanced drawing, and instructed to do
 * se through command 161.
 *
 * The other commands here set various vectors used for lighting (i.e. light
 * position and direction) but are not well understood.
 */

static void
mult_mtx4x4f_vec4f (vec4f_t res, mtx4x4f_t m, vec4f_t v)
{
	unsigned i;
	for (i = 0; i < 4; i++)
		res[i] = m[0][i] * v[0] + m[1][i] * v[1] + m[2][i] * v[2] + m[3][i] * v[3];
}

static float
norm_vec4 (vec4f_t v)
{
	return sqrtf (v[0]*v[0] + v[1]*v[1] + v[2]*v[2] + v[3]*v[3]);
}

/* 161	Set Matrix Vector
 *
 *	-------- ----UPNN -WW----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * U = Unknown
 *
 * P = Push
 *
 *	Pushes the uploaded modelview matrix in the modelview stack. Used
 *	for instanced drawing.
 *
 * W = Unknown
 *
 * N = Column index
 *
 *
 * 561	LOD: Set Vector
 *
 *	-------- ------NN -WW----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * N = Always 11b
 *
 * W = Always 11b
 *
 * Uploads a vector used for LOD computations.
 *
 *
 * 961	Light: Set Vector 9
 *
 *	-------- -------T TWW----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * T = Direction/Position/etc.
 * W = Unknown
 *
 *
 * B61	Light: Set Vector B
 *
 *	-------- -------- TWW----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * T = Direction/Position/etc.
 * W = Unknown
 */

I (0x161)
{
	hikaru_modelview_t *mv = &MV.table[MV.depth];
	hikaru_light_t *lit = &LIT.scratch;
	uint32_t push, elem;

	switch ((inst[0] >> 8) & 0xF) {

	case 0x1:
		/* Ignore conditional version. */
		if (inst[0] & 0x0008F000) {
			VK_ERROR ("@%08X: conditional modelview", PC);
			return;
		}

		push = (inst[0] >> 18) & 1;
		elem = (inst[0] >> 16) & 3;

		/* First element during upload. */
		if (elem == 3) {
			unsigned i, j;

			for (i = 0; i < 4; i++)
				for (j = 0; j < 4; j++)
					mv->mtx[i][j] = (i == j) ? 1.0f : 0.0;
		}

		/* We store the columns as rows to facilitate the
		 * translation to GL column-major convertion in the
		 * renderer. */
		mv->mtx[elem][0] = *(float *) &inst[1];
		mv->mtx[elem][1] = *(float *) &inst[2];
		mv->mtx[elem][2] = *(float *) &inst[3];
		mv->mtx[elem][3] = (elem == 3) ? 1.0f : 0.0f;

		/* Last element during upload. */
		if (elem == 0) {
			if (push) {
				MV.depth++;
				VK_ASSERT (MV.depth < NUM_MODELVIEWS);
			} else {
				MV.total = MV.depth + 1;
				MV.depth = 0;
			}
			VK_LOG ("DEBUG MTX depth=%u total=%u", MV.depth, MV.total);
		}
		break;
	case 0x5:
		{
			vec4f_t v, w;

			v[0] = *(float *) &inst[1];
			v[1] = *(float *) &inst[2];
			v[2] = *(float *) &inst[3];
			v[3] = 1.0f;

			mult_mtx4x4f_vec4f (w, MV.table[MV.depth].mtx, v);
			LOD.value = norm_vec4 (w);
		}
		break;
	case 0x9:
	case 0xB:
		switch (inst[0] & 0x000FF000) {
		case 0x00008000: /* Direction */
			lit->direction[0] = *(float *) &inst[1];
			lit->direction[1] = *(float *) &inst[2];
			lit->direction[2] = *(float *) &inst[3];
			lit->has_direction = 1;
			break;
		case 0x00010000: /* Position */
			lit->position[0] = *(float *) &inst[1];
			lit->position[1] = *(float *) &inst[2];
			lit->position[2] = *(float *) &inst[3];
			lit->has_position = 1;
			break;
		case 0x00016000: /* Use old position */
			/* XXX TODO */
			lit->position[0] = 0.0f;
			lit->position[1] = 0.0f;
			lit->position[2] = 0.0f;
			lit->has_position = 1;
			break;
		default:
			VK_ERROR ("CP @%08X: unhandled light 161 param: %08X",
			          PC, inst[0]);
			break;
		}
		lit->uploaded = 1;
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

D (0x161)
{
	uint32_t push, elem;

	switch ((inst[0] >> 8) & 0xF) {

	case 1:
		push = (inst[0] >> 18) & 1;
		elem = (inst[0] >> 16) & 3;

		UNHANDLED |= !!(inst[0] & 0xFFF0F000);

		DISASM ("mtx: set vector [%c %u (%f %f %f)]",
		        push ? 'P' : ' ', elem,
		        *(float *) &inst[1], *(float *) &inst[2], *(float *) &inst[3]);
		break;
	case 5:
		UNHANDLED |= !!(inst[0] & 0xFFFC0000);

		DISASM ("lod: set vector [%f %f %f]",
		        *(float *) &inst[1], *(float *) &inst[2], *(float *) &inst[3]);
		break;
	case 9:
		UNHANDLED |= !!(inst[0] & 0xFFFE0000);

		DISASM ("lit: set vector 9 [%f %f %f]",
		        *(float *) &inst[1], *(float *) &inst[2], *(float *) &inst[3]);
		break;
	case 0xB:
		UNHANDLED |= !!(inst[0] & 0xFFFF0000);

		DISASM ("lit: set vector B [%f %f %f]",
		        *(float *) &inst[1], *(float *) &inst[2], *(float *) &inst[3]);
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

/*
 * Materials
 * =========
 *
 * It supports flat, diffuse and phong shading. XXX more to come.
 */

static uint32_t
get_material_index (uint32_t *inst)
{
	return (inst[0] >> 16) & (NUM_MATERIALS - 1);
}

/* 091	Material: Set Primary Color
 *
 *	-------- -------- -------o oooooooo
 *	AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
 *
 * See PH:@0C0CF742.
 *
 *
 * 291	Material: Set Secondary Color
 *
 *	-------- -------- -------o oooooooo
 *	-------- BBBBBBBB GGGGGGGG RRRRRRRR
 *
 * See PH:@0C0CF742.
 *
 *
 * 491	Material: Set Shininess
 *
 *	-------- -------- -------o oooooooo
 *	SSSSSSSS BBBBBBBB GGGGGGGG RRRRRRRR
 *
 * S = Shininess
 *
 * See PH:@0C0CF798, PH:@0C01782C.
 *
 *
 * 691	Material: Set Material Color
 *
 *	RRRRRRRR RRRRRRRR -------o oooooooo
 *	BBBBBBBB BBBBBBBB GGGGGGGG GGGGGGGG
 *
 * See PH:@0C0CF7CC.
 *
 * NOTE: A91 and C91 are used in BRAVEFF title screen. They clearly alias A81
 * and C81.
 */

static void hikaru_gpu_inst_0x081 (hikaru_gpu_t *, uint32_t *);
static void hikaru_gpu_disasm_0x081 (hikaru_gpu_t *, uint32_t *);

I (0x091)
{
	hikaru_material_t *mat = &MAT.scratch;

	switch ((inst[0] >> 8) & 15) {
	case 0:
		mat->diffuse[0] = inst[1] & 0xFF;
		mat->diffuse[1] = (inst[1] >> 8) & 0xFF;
		mat->diffuse[2] = (inst[1] >> 16) & 0xFF;
		mat->diffuse[3] = inst[1] >> 24;
		mat->has_091 = 1;
	case 2:
		mat->ambient[0] = inst[1] & 0xFF;
		mat->ambient[1] = (inst[1] >> 8) & 0xFF;
		mat->ambient[2] = (inst[1] >> 16) & 0xFF;
		mat->has_291 = 1;
		break;
	case 4:
		mat->specular[0] = inst[1] & 0xFF;
		mat->specular[1] = (inst[1] >> 8) & 0xFF;
		mat->specular[2] = (inst[1] >> 16) & 0xFF;
		mat->specular[3] = inst[1] >> 24;
		mat->has_491 = 1;
		break;
	case 6:
		mat->unknown[0] = inst[0] >> 16;
		mat->unknown[1] = inst[1] & 0xFFFF;
		mat->unknown[2] = inst[1] >> 16;
		mat->has_691 = 1;
		break;
	case 0xA:
	case 0xC:
		hikaru_gpu_inst_0x081 (gpu, inst);
		/* XXX HACK */
		PC -= 4;
		return;
	default:
		VK_ASSERT (0);
		break;
	}
	mat->uploaded = 1;
}

D (0x091)
{
	switch ((inst[0] >> 8) & 15) {
	case 0:
	case 2:
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
	
		DISASM ("mat: set color %u [%08X]",
		        (inst[0] >> 9) & 1, inst[1]);
		break;
	case 4:
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
	
		DISASM ("mat: set shininess [%X %X]",
		        inst[1] >> 24, inst[1] & 0xFFFFFF);
		break;

	case 6:
		UNHANDLED |= !!(inst[0] & 0x0000F800);
	
		DISASM ("mat: set material [%X %X %X]",
		        inst[0] >> 16, inst[1] & 0xFFFF, inst[1] >> 16);
		break;
	case 0xA:
	case 0xC:
		hikaru_gpu_disasm_0x081 (gpu, inst);
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

/* 081	Material: Set Unknown
 *
 *	-------- ----mmmm ---n---o oooooooo
 *
 * n, m = Unknown
 *
 *
 * 881	Material: Set Flags
 *
 *	XXXXXXXX xyhatzSS -------o oooooooo
 *
 * X = Unknown
 *
 *	Used in BRAVEFF.
 *
 * x = Has X.
 *
 * y = Unknown.
 *
 *	Used in BRAVEFF.
 *
 * S = Shading mode
 *
 *	0 = Unlit.
 *	1 = Gouraud.
 *	2 = Flat?
 *
 * z = Depth blend (fog)
 *
 *	Probably decides whether the material is affected by fog, and that's it.
 *
 * t = Textured
 *
 * a = Alpha mode
 *
 *	Apparently only used for the skate in AIRTRIX.
 *
 * h = Highlight mode
 *
 *	Apparently unused.
 *
 * See PH:@0C0CF700.
 *
 *
 * A81	Material: Set Blending Mode
 *
 *	-------- ------mm -------o oooooooo
 *
 * m = Blending Mode
 *
 *	Apparently almost always zero. It is 2 only for lights and star patches
 *	in AIRTRIX.
 *
 * See PH:@0C0CF7FA.
 *
 *
 * C81	Material: Set Alpha Test
 *
 *	-------- --IIIIII -------o oooooooo
 *
 * I = Index intop the alpha threshold table (see instruction 154).
 *
 * See PH:@0C0CF868-@0C0CF876.
 */

I (0x081)
{
	hikaru_material_t *mat = &MAT.scratch;

	switch ((inst[0] >> 8) & 0xF) {
	case 0x0:
		mat->_081 = inst[0];
		mat->has_081 = 1;
		break;
	case 0x8:
		mat->_881 = inst[0];
		mat->has_881 = 1;
		break;
	case 0xA:
		mat->_A81 = inst[0];
		mat->has_A81 = 1;
		break;
	case 0xC:
		mat->_C81 = inst[0];
		mat->has_C81 = 1;
		break;
	}
	mat->uploaded = 1;
}

D (0x081)
{
	switch ((inst[0] >> 8) & 0xF) {
	case 0:
		UNHANDLED |= !!(inst[0] & 0xFFF0E000);

		DISASM ("mat: set unknown");
		break;
	case 8:
		UNHANDLED |= !!(inst[0] & 0x0000F000);

		DISASM ("mat: set flags [mode=%u zblend=%u tex=%u alpha=%u highl=%u y=%u x=%u X=%02X]",
		        (inst[0] >> 16) & 3,
		        (inst[0] >> 18) & 1,
		        (inst[0] >> 19) & 1,
		        (inst[0] >> 20) & 1,
		        (inst[0] >> 21) & 1,
		        (inst[0] >> 22) & 1,
		        (inst[0] >> 23) & 1,
		        inst[0] >> 24);
		break;
	case 0xA:
		UNHANDLED |= !!(inst[0] & 0xFFFCF000);

		DISASM ("mat: set blending mode [mode=%u]", (inst[0] >> 16) & 3);
		break;
	case 0xC:
		UNHANDLED |= !!(inst[0] & 0xFFC0F000);

		DISASM ("mat: set unknown [%x %X]",
		        (inst[0] >> 21) & 1, (inst[0] >> 16) & 0x1F);
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

/* 084	Commit Material
 *
 *	------nn nnnnnnnn ---1---o oooooooo
 *
 * n = Index
 *
 * See PH:@0C0153D4, PH:@0C0CF878.
 */

I (0x084)
{
	uint32_t index = MAT.base + get_material_index (inst);

	if (index >= NUM_MATERIALS) {
		VK_ERROR ("CP: material commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_MATERIALS);
		return;
	}

	MAT.table[index] = MAT.scratch;
}

D (0x084)
{
	UNHANDLED |= !!(inst[0] & 0xFF00E000);
	UNHANDLED |= !(inst[0] & 0x1000);

	DISASM ("mat: commit @base+%u", get_material_index (inst));
}

/* 083	Recall Material
 *
 *	---nnnnn nnnnnnnn ---A---o oooooooo
 *
 * n = Index
 * A = Active
 *
 * See @0C00657C, PH:@0C0CF882.
 */

I (0x083)
{
	uint32_t index = get_material_index (inst);

	if (!(inst[0] & 0x1000))
		MAT.base = index;
	else {
		index += MAT.base;
		if (index >= NUM_MATERIALS) {
			VK_ERROR ("CP: material recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_MATERIALS);
			return;
		}

		MAT.scratch = MAT.table[index];
		MAT.scratch.uploaded = 1;
	}
}

D (0x083)
{
	if (!(inst[0] & 0x1000)) {
		UNHANDLED |= !!(inst[0] & 0xC000E000);

		DISASM ("mat: set base %u", get_material_index (inst));
	} else {
		UNHANDLED |= !!(inst[0] & 0xFF00E000);

		DISASM ("mat: recall @base+%u", get_material_index (inst));
	}
}

/*
 * Texheads
 * ========
 *
 * Textures used for 3D rendering are stored (through the GPU IDMA) in the two
 * available TEXRAM banks.
 */

static uint32_t
get_texhead_index (uint32_t *inst)
{
	return (inst[0] >> 16) & (NUM_TEXHEADS - 1);
}

/* 0C1	Texhead: Set Bias
 *
 *	----VVVV VVVV--MM -------o oooooooo
 *
 * V = Unknown value.
 * M = Unknown mode.
 *
 *	Mode 0 is used frequently with values 0 and FF.
 *
 *	Mode 1 is used with a variety of values.
 *
 *	Mode 2 is only used in BRAVEFF.
 *
 *
 * 2C1	Texhead: Set Format/Size
 *
 *	UUUFFFrr wwHHHWWW uu-----o oooooooo
 *
 * U = Unknown
 *
 * F = Format
 *
 * r = Repeat mode
 *
 *	0 = Normal repeat.
 *	1 = Mirrored repeat.
 *
 *	Bit 0 for V, bit 1 for U. Only meaningful if wrapping is enabled.
 *
 * w = Wrap mode
 *
 *	0 = Clamp.
 *	1 = Wrap.
 *
 *	Bit 0 for V, bit 1 for U.
 *
 * H = log16 of Height 
 * W = log16 of Width
 *
 * u = Unknown
 *
 * See PH:@0C015BCC.
 *
 *
 * 4C1	Texhead: Set Slot
 *
 *	nnnnnnnn mmmmmmmm ---b---o oooooooo
 *
 * n = Slot Y
 * m = Slot X
 * b = TEXRAM bank
 *
 * NOTE: for some reason the BOOTROM uploads a couple of 2C1/4C1 instructions
 * with their parameters swapped. This hasn't been seen in any game so far.
 *
 * See PH:@0C015BA0.
 */

I (0x0C1)
{
	hikaru_texhead_t *th = &TEX.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		th->_0C1 = inst[0];
		th->has_0C1 = 1;
		break;
	case 2:
		th->_2C1 = inst[0];
		th->has_2C1 = 1;
		break;
	case 4:
		th->_4C1 = inst[0];
		th->has_4C1 = 1;
		break;
	default:
		VK_ASSERT (0);
		break;
	}
	th->uploaded = 1;
}

D (0x0C1)
{
	switch ((inst[0] >> 8) & 7) {
	case 0:
		UNHANDLED |= !!(inst[0] & 0xF00CF800);

		DISASM ("tex: set bias [mode=%u %X]",
		        (inst[0] >> 16) & 0xF, (inst[0] >> 20) & 0xFF);
		break;

	case 2:
		UNHANDLED |= !!(inst[0] & 0x00003800);

		DISASM ("tex: set format [%ux%u fmt=%u wrap=(%u %u|%u %u) unk=%X]",
		        16 << ((inst[0] >> 16) & 7),
		        16 << ((inst[0] >> 19) & 7),
		        (inst[0] >> 26) & 7,
		        (inst[0] >> 22) & 1,
		        (inst[0] >> 23) & 1,
		        (inst[0] >> 24) & 1,
		        (inst[0] >> 25) & 1,
		        ((inst[0] >> 14) & 3) | (((inst[0] >> 29) & 7) << 2));
		break;
	case 4:
		DISASM ("tex: set slot [bank=%u (%X,%X)]",
		        (inst[0] >> 12) & 1,(inst[0] >> 16) & 0xFF, inst[0] >> 24);

		UNHANDLED |= !!(inst[0] & 0x0000E000);
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

/* 0C4	Commit Texhead
 *
 *	------nn nnnnnnnn ---uoooo oooooooo
 *
 * n = Index
 * u = Unknown; always 1?
 *
 * See PH:@0C01545C. 
 */

I (0x0C4)
{
	uint32_t index = TEX.base + get_texhead_index (inst);

	if (index >= NUM_TEXHEADS) {
		VK_ERROR ("CP: texhead commit index exceedes MAX (%u >= %u), skipping",
		          index, NUM_MATERIALS);
		return;
	}

	TEX.table[index] = TEX.scratch;
}

D (0x0C4)
{
	UNHANDLED |= !!(inst[0] & 0xFF00E000);
	UNHANDLED |= ((inst[0] & 0x1000) != 0x1000);

	DISASM ("tex: commit @base+%u", get_texhead_index (inst));

}

/* 0C3	Recall Texhead
 *
 *	--nnnnnn nnnnnnnn ---M---o oooooooo
 *
 * n = Index
 * M = Modifier: 0 = set base only, 1 = recall for real
 *
 * XXX n here is likely too large.
 */

I (0x0C3)
{
	uint32_t index = get_texhead_index (inst);

	if (!(inst[0] & 0x1000))
		TEX.base = index;
	else {
		index += TEX.base;
		if (index >= NUM_TEXHEADS) {
			VK_ERROR ("CP: texhead recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_TEXHEADS);
			return;
		}

		TEX.scratch = TEX.table[index];
		TEX.scratch.uploaded = 1;
	}
}

D (0x0C3)
{
	if (!(inst[0] & 0x1000)) {
		UNHANDLED |= !!(inst[0] & 0xC000E000);

		DISASM ("tex: set base %u", get_texhead_index (inst));
	} else {
		UNHANDLED |= !!(inst[0] & 0xFF00E000);

		DISASM ("tex: recall @base+%u", get_texhead_index (inst));
	}
}

/*
 * Lights
 * ======
 *
 * According to the system16.com specs, the hardware supports 1024 lights per
 * scene, and 4 lights per polygon. It supports several light types (ambient,
 * spot, etc.) and several emission types (constant, infinite linear, square,
 * reciprocal, and reciprocal squared.)
 *
 * AFAICS, the hardware supports two light-related objects: lights and
 * lightsets. A light specifies the position, direction, emission properties
 * and more of a single light. A lightset specifies a set of (up to) four
 * lights that insist on the mesh being rendered. This setup is consistent with
 * the system16 specs.
 */

static uint32_t
get_light_index (uint32_t *inst)
{
	return (inst[0] >> 16) & (NUM_LIGHTS - 1);
}

static uint32_t
get_lightset_index (uint32_t *inst)
{
	return (inst[0] >> 16) & (NUM_LIGHTSETS - 1);
}

/* 061	Light: Set Attenuation
 *
 *	-------- ------TT -------o oooooooo
 *	PPPPPPPP PPPPPPPP PPPPPPPP PPPPPPPP
 *	QQQQQQQQ QQQQQQQQ QQQQQQQQ QQQQQQQQ
 *	-------- -------- -------- --------
 *
 * T = Attenuation type
 *
 *	0 = Linear (or infinite if P = Q = 1)
 *	1 = Square
 *	2 = Inverse linear
 *	3 = Inverse square
 *
 * P, Q = Attenuation parameters
 *
 * For attenuation type 0, P and Q are:
 *
 *  P = 1.0f / (FR4 - FR5)
 *  Q = -FR5
 *
 * For attenuation type 1, P and Q are:
 *
 *  P = 1.0f / (FR4**2 - FR5**2)
 *  Q = -FR5**2
 *
 * For attenuation type 2, P and Q are:
 *
 *  P = (FR4 * FR5) / (FR4 - FR5)
 *  Q = 1.0f / |FR5|
 *
 * For attenuation type 3, P and Q are:
 *
 *  P = (FR4**2 * FR5**2) / (FR5**2 - FR4**2)
 *  Q = 1.0 / |FR5**2|
 */

I (0x061)
{
	hikaru_light_t *lit = &LIT.scratch;

	lit->att_type       = (inst[0] >> 16) & 3;
	lit->attenuation[0] = *(float *) &inst[1];
	lit->attenuation[1] = *(float *) &inst[2];

	lit->has_061 = 1;
	lit->has_position = 0;
	lit->has_direction = 0;
	lit->uploaded = 1;
}

D (0x061)
{
	UNHANDLED |= !!(inst[0] & 0xFFFCF000);

	DISASM ("lit: set attenuation [%u p=%f q=%f]",
	        (inst[0] >> 16) & 3, *(float *) &inst[1], *(float *) &inst[2]);
}

/* 051	Light: Set Diffuse
 *
 *	-------- D---XXXX -------o oooooooo
 *	--BBBBBB BBBBGGGG GGGGGGRR RRRRRRRR
 *
 * D = Disabled?
 * X = Index/Mode.
 *
 * See PH:@0C0178C6; for a,b,c computation see PH:@0C03DC66.
 *
 *
 * 451	Light: Set Specular
 *
 *	-------D -------- -------o oooooooo
 *	-------- BBBBBBBB GGGGGGGG RRRRRRRR
 *
 * D = Disabled.
 *
 * See PH:@0C017A7C, PH:@0C017B6C, PH:@0C017C58, PH:@0C017CD4, PH:@0C017D64.
 */

I (0x051)
{
	hikaru_light_t *lit = &LIT.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		lit->_051_bit   = (inst[0] >> 23) & 1;
		lit->_051_index = (inst[0] >> 16) & 0xF;
		lit->diffuse[0] = inst[1] & 0x3FF;
		lit->diffuse[1] = (inst[1] >> 10) & 0x3FF;
		lit->diffuse[2] = (inst[1] >> 20) & 0x3FF;
		lit->has_051 = 1;
		break;
	case 4:
		lit->has_specular  = ((inst[0] >> 24) & 1) ^ 1;
		lit->specular[0] = inst[1] & 0xFF;
		lit->specular[1] = (inst[1] >> 8) & 0xFF;
		lit->specular[2] = (inst[1] >> 16) & 0xFF;
		lit->has_451 = 1;
		break;
	default:
		VK_ASSERT (0);
		break;
	}
	lit->uploaded = 1;
}

D (0x051)
{
	switch ((inst[0] >> 8) & 7) {
	case 0:
		UNHANDLED |= !!(inst[0] & 0xFF70F000);
		UNHANDLED |= !!(inst[1] & 0xC0000000);

		DISASM ("lit: set diffuse [%X]", inst[1]);
		break;
	case 4:
		UNHANDLED |= !!(inst[0] & 0xFEFFF000);
		
		DISASM ("lit: set specular [%X]", inst[1]);
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

/* 006	Light: Unknown
 *
 *	-------- -------- -------o oooooooo
 */

I (0x006)
{
}

D (0x006)
{
	UNHANDLED |= !!(inst[0] & 0xFFFFF000);

	DISASM ("lit: unknown");
}

/* 046	Light: Unknown
 *
 *	-------- -------n ----oooo oooooooo
 *
 * n = Unknown
 */

I (0x046)
{
}

D (0x046)
{
	UNHANDLED |= !!(inst[0] & 0xFFFEF000);

	DISASM ("lit: unknown [%u]", (inst[0] >> 16) & 1);
}

/* 104	Commit Light
 *
 *	------nn nnnnnnnn ----oooo oooooooo
 *
 * n = Index
 */

I (0x104)
{
	uint32_t index = get_light_index (inst);

	LIT.table[index] = LIT.scratch;
}

D (0x104)
{
	UNHANDLED |= !!(inst[0] & 0xFC00F000);

	DISASM ("lit: commit @%u", get_light_index (inst));
}

/* 064  Commit Lightset
 *
 *      -------- nnnnnnnn ---M---o oooooooo
 *      ------bb bbbbbbbb ------aa aaaaaaaa
 *      ------dd dddddddd ------cc cccccccc
 *	-------- -------- -------- -------- 
 *
 * M = Unknown (0 in the BOOTROM, 1 elsewhere)
 * n = Lightset index
 * a,b,c,d = Indices of four lights
 *
 * See PH:@0C017DF0.
 */

I (0x064)
{
	hikaru_lightset_t *ls;
	uint32_t index = LIT.base + get_lightset_index (inst);

	if (index >= NUM_LIGHTSETS) {
		VK_ERROR ("CP: lightset commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_LIGHTSETS);
		return;
	}

	ls = &LIT.sets[index];

	ls->lights[0] = LIT.table[inst[1] & (NUM_LIGHTS - 1)];
	ls->lights[1] = LIT.table[(inst[1] >> 16) & (NUM_LIGHTS - 1)];
	ls->lights[2] = LIT.table[inst[2] & (NUM_LIGHTS - 1)];
	ls->lights[3] = LIT.table[(inst[2] >> 16) & (NUM_LIGHTS - 1)];
	ls->set = 1;
}

D (0x064)
{
	UNHANDLED |= !!(inst[0] & 0xFF00E000);
	UNHANDLED |= ((inst[0] & 0x1000) != 0x1000);
	UNHANDLED |= !!(inst[1] & 0xFC00FC00);
	UNHANDLED |= !!(inst[2] & 0xFC00FC00);

	DISASM ("lit: commit set @base+%u [%u %u %u %u]",
	        get_lightset_index (inst),
	        inst[1] & (NUM_LIGHTS - 1), (inst[1] >> 16) & (NUM_LIGHTS - 1),
	        inst[2] & (NUM_LIGHTS - 1), (inst[2] >> 16) & (NUM_LIGHTS - 1));
}

/* 043	Recall Lightset
 *
 *	----DDDD nnnnnnnn ---A---o oooooooo
 *
 * A = Active
 * D = Disabled lights mask
 * n = Index
 */

I (0x043)
{
	uint32_t index = get_lightset_index (inst);

	if (!(inst[0] & 0x1000))
		LIT.base = index;
	else {
		index += LIT.base;
		if (index >= NUM_LIGHTSETS) {
			VK_ERROR ("CP: lightset recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_LIGHTSETS);
			return;
		}

		LIT.scratchset = LIT.sets[index];
		LIT.scratchset.mask = (inst[0] >> 24) & 0xF;
		LIT.scratchset.uploaded = 1;
	}
}

D (0x043)
{
	UNHANDLED |= !!(inst[0] & 0xF000E000);

	if (!(inst[0] & 0x1000))
		DISASM ("lit: set set base %u", get_lightset_index (inst));
	else
		DISASM ("lit: recall set @base+%u [mask=%X]",
		        get_lightset_index (inst), (inst[0] >> 24) & 0xFF);
}

/*
 * Meshes
 * ======
 *
 * This class of instructions pushes (or otherwise deals with) vertex
 * data to the transformation/rasterization pipeline.
 */

/* 101	Mesh: Set Unknown (Set Light Unknown?)
 *
 *	----nnuu uuuuuuuu ----000o oooooooo
 *
 * n, u = Unknown
 *
 * 3FF is exactly the number of lights... Perhaps 'set sunlight'?
 *
 * See @0C008040, PH:@0C016418, PH:@0C016446.
 *
 *
 * 301	Mesh: Set Unknown
 *
 *	-------- unnnnnnn ----oooo oooooooo
 *
 * u, n = Unknown, n != 1
 *
 *
 * 501	Mesh: Set Unknown                                                             
 *                                                                             
 *	-------- ---ppppp -----oo oooooooo
 *                                                                             
 * p = Param, unknown.                                                         
 *                                                                             
 * Used by the BOOTROM.                                                        
 *                                                                             
 * See AT:@0C0C841C, the table that holds the constant parameters to the 501   
 * command, set by the routine AT:@0C049BA6 in variable AT:@0C61404C, which    
 * is wrapped into the 501 command in AT:@0C049D08.                            
 *
 *
 * 901	Mesh: Set Precision (Static)
 *
 *	-------- pppppppp ----100o oooooooo
 *
 * Information kindly provided by CaH4e3.
 */

I (0x101)
{
	uint32_t log;

	switch ((inst[0] >> 8) & 0xF) {
	case 1:
		break;
	case 3:
		break;
	case 5:
		break;
	case 9:
		log = (inst[0] >> 16) & 0xFF;
		POLY.static_mesh_precision = 1.0f / (1 << (0x8F - log - 2));
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

D (0x101)
{
	uint32_t log;
	float precision;

	switch ((inst[0] >> 8) & 0xF) {
	case 1:
		UNHANDLED |= !!(inst[0] & 0xF000F000);

		DISASM ("mesh: set unknown [%u]", (inst[0] >> 16) & 0x3FF);
		break;
	case 3:
		UNHANDLED |= !!(inst[0] & 0xFF00F000);

		DISASM ("mesh: set unknown [%u]", (inst[0] >> 16) & 0xFF);
		break;
	case 5:	
		UNHANDLED |= !!(inst[0] & 0xFFE0F000);

		DISASM ("mesh: set unknown [%u]", (inst[0] >> 16) & 0x1F);
		break;
	case 9:
		log = (inst[0] >> 16) & 0xFF;
		precision = 1.0f / (1 << (0x8F - log - 2));

		UNHANDLED |= !!(inst[0] & 0xFF00F000);

		DISASM ("mesh: set precision s [%u %f]", log, precision);	
		break;
	default:
		VK_ASSERT (0);
		break;
	}

}

/* 103	Set Poly Type
 * 113	Set Poly Type
 *
 *	AAAAAAAA -------- -------o oooxoooo
 *
 * A = Base mesh alpha value
 * x = Unknown
 *
 *  3: Opaque. Alpha is ignored.
 *  9: Punch-through. Alpha is ignored.
 *  D: Translucent.
 *
 * Information kindly contributed by DreamZzz.
 *
 * See AT:@0C049CDA, PH:@0C0173CA, AT:@0C69A220.
 */

static void
get_poly_type (uint32_t *inst, uint32_t *type, float *alpha)
{
	*type	= (inst[0] >> 9) & 7;
	*alpha	= (inst[0] >> 24) * (1.0f / 255.0f);
}

I (0x103)
{
	uint32_t type;
	float alpha;

	get_poly_type (inst, &type, &alpha);

	POLY.type  = type;
	POLY.alpha = alpha;
}

D (0x103)
{
	static const char *poly_type_name[8] = {
		"invalid 0",
		"opaque",
		"shadow A",
		"shadow B",
		"transparent",
		"background",
		"translucent",
		"invalid 7"
	};
	uint32_t type;
	float alpha;

	get_poly_type (inst, &type, &alpha);

	UNHANDLED |= !!(inst[0] & 0x00FFF000);

	DISASM ("mesh: set poly type [%s alpha=%f]", poly_type_name[type], alpha);
}

/* 12x	Mesh: Push Position (Static)
 * 1Ax	Mesh: Push Position (Dynamic)
 * 1Bx	Mesh: Push All (Position, Normal, Texcoords) (Dynamic)
 *
 *
 * They appear to have a common 32-bit header:
 *
 *	AAAAAAAA C---x--- uuuSTTTo oooootpW
 *
 * A = Vertex alpha
 *
 * C = Don't Cull
 *
 *     Disables face culling for this mesh.
 *
 * x = Unknown
 *
 *	Used in PHARRIER (the big SEGA text).
 *
 * u = Unknown
 *
 *     No idea.
 *
 * S = Unknown
 *
 *     Seemingly used for shadows in AIRTRIX (attract mode) and for edges of
 *     flames/smoke in BRAVEFF.
 *
 * T = Triangle
 *
 *     The only observed values so far are 0 and 7.
 *
 *     If 0, the vertex is pushed to the GPU vertex buffer. If 7, the vertex is
 *     pushed and defines a triangle along with the two previously pushed
 *     vertices.
 *
 * t = Texcoord pivot?
 *
 *     Apparently only used by 1Bx, which includes texcoord info.
 *
 * p = Position pivot
 *
 *     When 0, the vertex is linked to the previous two according to the
 *     winding bit. When 1, the vertex at offset -2 is kept unchanged in the
 *     vertex buffer, and acts as a pivot for building a triangle fan.
 *
 * W = Winding
 *
 *     Triangle winding specifies whether the vertices composing the
 *     to-be-drawn triangle are to be in the order (0, -1, -2), and W is 0
 *     in this case, or (0, -2, -1), and W is 1 in this case.
 *
 *     If vertex in position -2 is a pivot, it is treated as if it wasn't. [?]
 *
 * For 12x, the rest of the instruction looks like:
 *
 *	-------- -------- -------- --------
 *	xxxxxxxx xxxxxxxx ??????uu uuuuuuuu
 *	yyyyyyyy yyyyyyyy ??????vv vvvvvvvv
 *	zzzzzzzz zzzzzzzz ??????ww wwwwwwww
 *
 * x,y,z = Position
 * u,v,w = Normal
 *
 * For 1BX, the rest of the instruction looks like:
 *
 *	-------- -------- -------- --------
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * x,y,z = Position
 *
 * For 1BX, the rest of the instruction looks like:
 *
 *	-------- -------- -------- --------
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *	ssssssss ssssssss tttttttt tttttttt
 *	uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvvv
 *	wwwwwwww wwwwwwww wwwwwwww wwwwwwww
 *
 * x,y,z = Position
 * u,v,w = Normal
 * s,t = Texcoords
 *
 * Meshes come in two flavors: dynamic and static. Dynamic meshes are specified
 * with normal IEEE457 floating-point values. Static meshes are specified with
 * variable fixed-point values; the fixed-point precision is defined by command
 * 901.
 *
 * Information on the existence of static/dynamic mesh varieties, and info on
 * the fixed-point decoding kindly provided by CaH4e3.
 */

I (0x12C)
{
	hikaru_vertex_t v;

	v.info.full = inst[0];

	VK_ASSERT (POLY.static_mesh_precision > 0.0f);

	v.position[0] = (int16_t)(inst[1] >> 16) * POLY.static_mesh_precision;
	v.position[1] = (int16_t)(inst[2] >> 16) * POLY.static_mesh_precision;
	v.position[2] = (int16_t)(inst[3] >> 16) * POLY.static_mesh_precision;

	v.normal[0] = (int16_t)((inst[1] & 0x3FF) << 6) / 16384.0f;
	v.normal[1] = (int16_t)((inst[2] & 0x3FF) << 6) / 16384.0f;
	v.normal[2] = (int16_t)((inst[3] & 0x3FF) << 6) / 16384.0f;

	hikaru_renderer_push_vertices (HR, &v, HR_PUSH_POS | HR_PUSH_NRM, 1);
}

D (0x12C)
{
	UNHANDLED |= !!(inst[0] & 0x007F0000);

	DISASM ("mesh: push position s");
}

I (0x1AC)
{
	hikaru_vertex_t v;

	v.info.full = inst[0];

	v.position[0] = *(float *) &inst[1];
	v.position[1] = *(float *) &inst[2];
	v.position[2] = *(float *) &inst[3];

	hikaru_renderer_push_vertices (HR, &v, HR_PUSH_POS, 1);
}

D (0x1AC)
{
	UNHANDLED |= !!(inst[0] & 0x007F0000);

	DISASM ("mesh: push position d");
}

I (0x1B8)
{
	hikaru_vertex_t v;

	v.info.full = inst[0];

	v.position[0] = *(float *) &inst[1];
	v.position[1] = *(float *) &inst[2];
	v.position[2] = *(float *) &inst[3];

	v.normal[0] = *(float *) &inst[5];
	v.normal[1] = *(float *) &inst[6];
	v.normal[2] = *(float *) &inst[7];

	v.texcoords[0] = ((int16_t) inst[4]) / 16.0f;
	v.texcoords[1] = ((int16_t) (inst[4] >> 16)) / 16.0f;

	hikaru_renderer_push_vertices (HR, &v, HR_PUSH_POS | HR_PUSH_NRM | HR_PUSH_TXC, 1);
}

D (0x1B8)
{
	UNHANDLED |= !!(inst[0] & 0x00770000);

	DISASM ("mesh: push all d");
}

/* 0E8	Mesh: Push Texcoords 3
 *
 *	-------- -------x ----WWWo oooooooC
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *
 * The interaction of U, u, P, W, with the ones specified by the push position/
 * push all instructions is still unknown.
 *
 * u, v = Texcoords for three points.
 */

I (0x0E8)
{
	hikaru_vertex_t vs[3];
	unsigned i;

	for (i = 0; i < 3; i++) {
		vs[i].info.full = inst[0];

		vs[i].texcoords[0] = ((int16_t) inst[i+1]) / 16.0f;
		vs[i].texcoords[1] = ((int16_t) (inst[i+1] >> 16)) / 16.0f;
	}

	hikaru_renderer_push_vertices (HR, &vs[0], HR_PUSH_TXC, 3);
}

D (0x0E8)
{

	UNHANDLED |= !!(inst[0] & 0xFFFEF000);

	DISASM ("mesh: push texcoords 3");
}

/* 158	Mesh: Push Texcoords 1
 *
 *	-------- ?------- ----???o ooooo??C
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *
 * u, v = Texcoords for one point.
 */

I (0x158)
{
	hikaru_vertex_t v;

	v.info.full = inst[0];

	v.texcoords[0] = ((int16_t) inst[1]) / 16.0f;
	v.texcoords[1] = ((int16_t) (inst[1] >> 16)) / 16.0f;

	hikaru_renderer_push_vertices (HR, &v, HR_PUSH_TXC, 1);
}

D (0x158)
{
	UNHANDLED |= !!(inst[0] & 0xFF7FF000);

	DISASM ("mesh: push texcoords 1");
}

/****************************************************************************
 Unknown
****************************************************************************/

/* 181	FB: Set Blending
 *
 *	-------E AAAAAAAA -------o oooooooo
 *
 * E = Enable blending
 * A = Blending factor
 *
 *
 * 781	FB: Set Combiner
 *
 *	-----ENN -----enn -------o oooooooo
 *
 * E = Buffer A, Enable (?)
 * N = Buffer A, Select first buffer
 *
 * e = Buffer B, Enable (?)
 * n = Buffer B, Select second buffer
 *
 * Determines how to linearly combine the framebuffers to obtain the 3D scene.
 * The equation is given by:
 *
 *    result = factor * A + (1 - factor) * B
 *
 * Here A and B can be either the front/back buffer or a 2D layer. The numering
 * should be the same as that of registers 1A000180-1A00019C.
 *
 * The values uploaded here depend on the state of 1A00001C bits 23:24 and
 * 1A000020 bit 0. See @0C0065D6, PH:@0C016336, PH:@0C038952, PH:@0C015B50.
 */

I (0x181)
{
	switch ((inst[0] >> 8) & 7) {
	case 1:
		gpu->fb_config._181 = inst[0];
		break;
	case 7:
		gpu->fb_config._781 = inst[0];
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

D (0x181)
{
	switch ((inst[0] >> 8) & 7) {
	case 1:
		UNHANDLED |= !!(inst[0] & 0xFE00F800);

		DISASM ("fb: set blending (%u %X)",
		        (inst[0] >> 24) & 1, (inst[0] >> 16) & 0xFF);
		break;
	case 7:
		UNHANDLED |= !!(inst[0] & 0xF8F8F800);

		DISASM ("fb: set combiner (%X %X)",
		        (inst[0] >> 24) & 7, (inst[0] >> 16) & 7);
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

/* 088	Flush
 *
 *	-------- U------- ----xxxo oooooooo
 *
 * U = Unknown
 * x = Unknown
 *
 * Always comes as the last instruction. Perhaps some kind of 'flush all'
 * or 'raise IRQ' command or something like that. If it is a 'flush all'
 * command, it may set some GPU ports not set by 1C2 (1A000024 perhaps.)
 */

I (0x088)
{
}

D (0x088)
{
	UNHANDLED |= !!(inst[0] & 0xFF7FF000);

	DISASM ("unk: unknown");
}

/* 154	Mat: Set Alpha Threshold
 *
 *	-------- --IIIIII -------o oooooooo
 *	HHHHHHHH HHHHHHHH HHHHHHHH LLLLLLLL
 *
 * I = Index
 * L = Alpha low threshold
 * H = Alpha high threshold
 *
 * See PH:@0C017798, PH:@0C0CF868. Used by instruction C81.
 */

I (0x154)
{
	ATABLE[(inst[0] >> 16) & 0x3F].full = inst[1];
}

D (0x154)
{
	UNHANDLED |= !!(inst[0] & 0xFFC0F000);

	DISASM ("mat: set alpha thresh [%u (%X %X)]",
	        (inst[0] >> 16) & 0x3F, inst[1] & 0xFF, inst[1] >> 8);
}

/* 194	Light: Set Table
 *
 *	------NN ---MMMMM -------o oooooooo
 *	LLLLLLLL LLLLLLLL HHHHHHHH HHHHHHHH
 *
 * N, M = Indices
 * L = Data, Lo
 * H = Data, Hi
 *
 * NOTE: definitely related to lighting; possibly spotlight angles.
 *
 * See PH:@0C017A3E.
 */

I (0x194)
{
	uint32_t index1 = (inst[0] >> 24) & 3;
	uint32_t index2 = (inst[0] >> 16) & 0x1F;

	LTABLE[index1][index2].full = inst[1];
}

D (0x194)
{
	UNHANDLED |= !!(inst[0] & 0xFCE0F000);

	DISASM ("light: set table [%u:%u lo=%X hi=%X]",
	        (inst[0] >> 24) & 3, (inst[0] >> 16) & 0x1F,
	        inst[1] >> 16, inst[1] & 0xFFFF);
}

/* 3A1	Set Lo Addresses
 *
 *	-------- -------- -----01o oooooooo
 *	llllllll llllllll llllllll llllllll
 *	LLLLLLLL LLLLLLLL LLLLLLLL LLLLLLLL
 *      -------- -------- -------- --------
 *
 *
 * 5A1	Set Hi Addresses
 *
 *	-------- -------- -----10o oooooooo
 *	uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu
 *	UUUUUUUU UUUUUUUU UUUUUUUU UUUUUUUU
 *      -------- -------- -------- --------
 *
 * l,L,h,H = Addresses? Possibly watermarks?
 *
 * See PH:@0C016308 for both.
 */

I (0x1A1)
{
}

D (0x1A1)
{
	UNHANDLED |= !!(inst[0] & 0xFFFFF000);
	UNHANDLED |= (inst[3] != 0);

	DISASM ("unk: set address [%08X %08X]", inst[1], inst[2]);
}

/* 0D1	Set Unknown
 *
 *	???????? ??????aa -----11o oooooooo
 *	bbbbbbbb bbbbbbbb cccccccc cccccccc
 *
 * These come in quartets. May be related to matrices. See PH:@0C015C3E. Note
 * that the values b and c here come from FPU computations, see PH:@0C0FF970.
 */

I (0x0D1)
{
}

D (0x0D1)
{
	UNHANDLED |= !!(inst[0] & 0xFFFCF000);

	DISASM ("unk: unknown [%X %X %X]",
	        inst[0] >> 16, inst[1] & 0xFFFF, inst[1] >> 16);
}

/****************************************************************************
 Opcodes
****************************************************************************/

#define K(op_, base_op_, flags_) \
	{ op_, flags_, hikaru_gpu_inst_##base_op_, hikaru_gpu_disasm_##base_op_ }

static const struct {
	uint32_t op;
	uint16_t flags;
	void (* handler)(hikaru_gpu_t *gpu, uint32_t *inst);
	void (* disasm)(hikaru_gpu_t *gpu, uint32_t *inst);
} insns_desc[] = {
	/* 0x00 */
	K(0x000, 0x000, FLAG_CONTINUE),
	K(0x003, 0x003, 0),
	K(0x004, 0x004, 0),
	K(0x005, 0x005, 0),
	K(0x006, 0x006, 0),
	K(0x011, 0x011, 0),
	K(0x012, 0x012, FLAG_JUMP),
	K(0x021, 0x021, 0),
	/* 0x40 */
	K(0x043, 0x043, 0),
	K(0x046, 0x046, 0),
	K(0x051, 0x051, 0),
	K(0x052, 0x052, FLAG_JUMP),
	K(0x055, 0x055, 0),
	K(0x061, 0x061, 0),
	K(0x064, 0x064, 0),
	/* 0x80 */
	K(0x081, 0x081, FLAG_CONTINUE),
	K(0x082, 0x082, FLAG_JUMP),
	K(0x083, 0x083, FLAG_CONTINUE),
	K(0x084, 0x084, 0),
	K(0x088, 0x088, 0),
	K(0x091, 0x091, FLAG_CONTINUE),
	K(0x095, 0x095, 0),
	/* 0xC0 */
	K(0x0C1, 0x0C1, 0),
	K(0x0C3, 0x0C3, 0),
	K(0x0C4, 0x0C4, 0),
	K(0x0D1, 0x0D1, 0),
	K(0x0E8, 0x0E8, FLAG_PUSH),
	K(0x0E9, 0x0E8, FLAG_PUSH),
	/* 0x100 */
	K(0x101, 0x101, 0),
	K(0x103, 0x103, 0),
	K(0x104, 0x104, 0),
	K(0x113, 0x103, 0),
	K(0x12C, 0x12C, FLAG_PUSH | FLAG_STATIC),
	K(0x12D, 0x12C, FLAG_PUSH | FLAG_STATIC),
	K(0x12E, 0x12C, FLAG_PUSH | FLAG_STATIC),
	K(0x12F, 0x12C, FLAG_PUSH | FLAG_STATIC),
	/* 0x140 */
	K(0x154, 0x154, 0),
	K(0x158, 0x158, FLAG_PUSH),
	K(0x159, 0x158, FLAG_PUSH),
	K(0x15A, 0x158, FLAG_PUSH),
	K(0x15B, 0x158, FLAG_PUSH),
	K(0x161, 0x161, 0),
	/* 0x180 */
	K(0x181, 0x181, 0),
	K(0x191, 0x191, 0),
	K(0x194, 0x194, 0),
	K(0x1A1, 0x1A1, 0),
	K(0x1AC, 0x1AC, FLAG_PUSH),
	K(0x1AD, 0x1AC, FLAG_PUSH),
	K(0x1AE, 0x1AC, FLAG_PUSH),
	K(0x1AF, 0x1AC, FLAG_PUSH),
	K(0x1B8, 0x1B8, FLAG_PUSH),
	K(0x1B9, 0x1B8, FLAG_PUSH),
	K(0x1BA, 0x1B8, FLAG_PUSH),
	K(0x1BB, 0x1B8, FLAG_PUSH),
	K(0x1BC, 0x1B8, FLAG_PUSH),
	K(0x1BD, 0x1B8, FLAG_PUSH),
	K(0x1BE, 0x1B8, FLAG_PUSH),
	K(0x1BF, 0x1B8, FLAG_PUSH),
	/* 0x1C0 */
	K(0x1C2, 0x1C2, FLAG_JUMP)
};

#undef K

void
hikaru_gpu_cp_init (hikaru_gpu_t *gpu)
{
	unsigned i;
	for (i = 0; i < 0x200; i++) {
		insns[i].handler = NULL;
		insns[i].flags = FLAG_INVALID;
		disasm[i] = NULL;
	}
	for (i = 0; i < NUMELEM (insns_desc); i++) {
		uint32_t op = insns_desc[i].op;
		insns[op].handler = insns_desc[i].handler;
		insns[op].flags   = insns_desc[i].flags;
		disasm[op]        = insns_desc[i].disasm;
	}
}

