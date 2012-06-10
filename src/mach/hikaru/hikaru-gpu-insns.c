/* 
 * Valkyrie
 * Copyright (C) 2011, 2012, Stefano Teso
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

#include <math.h>

#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-gpu-private.h"
#include "mach/hikaru/hikaru-renderer.h"

/*
 * Command Processor
 * =================
 *
 * TODO. Controlled by 15000058. Generates GPU 15 IRQ 4 on termination. No
 * idea if it starts processing immediately or on, e.g., vblank-in; no idea
 * about relations to possible (likely) double buffering relation to its
 * timing; will need a lot more investigative work to be solved...
 *
 *
 * Control Flow
 * ============
 *
 * TODO. The GPU is able to call subroutines. So it is likely to hold a
 * stack somewhere: the current candidates are 1500007{4,8}. No idea if and
 * what state changes when subroutines are called (like the current matrix
 * or anything else.)
 *
 *
 * State and Commit/Recall
 * =======================
 *
 * Most commands manipulate the following GPU objects: viewports, materials,
 * textures (which are called texheads in PHARRIER; we follow this convention
 * in the source) and lights/lightsets (this terminology is also found in
 * PHARRIER.)
 *
 * My current understanding is as follows: the GPU holds stores information
 * about a bunch of these objects, that is, it can remember up to 8 viewports,
 * 1024 lights(ets), N materials and M texheads (XXX N and M are yet to be
 * determined.)
 *
 * At every one time there's only one active object for each category (one
 * active viewport, one active material, one active texhead, one active
 * lightset.) The active object influences the rendering of vertex data
 * pushed by the GPU.
 *
 * The instructions below modify the properties of the currently active and
 * non-active objects according to a commit/recall mechanism. Basically:
 *
 * - The 'set' instructions set properties of the currently active object
 *   (for instance the width/height/format of the active texhead.)
 *
 * - The 'commit' instructions copy the properties of the active object to
 *   the GPU storage space, to an object identified by an index (e.g., copy
 *   the params of the active viewport to the Nth stored viewport.)
 *
 * - The 'recall' instructions take a non-active object, identified by an
 *   index, and make it active. Alternatively, a 'recall' instruction can
 *   be used to set an offset to be added to the index of the following
 *   'commit' operations (or something like that.)
 *
 * The rendering state is manipulated using sequences like recall+set+commit
 * or recall-offset+set+commit.
 *
 * Of course, take all of this with a grain of salt. ;-)
 *
 * Control Flow
 * ============
 *
 * No conditionals? See 000, 012, 052, 082, 1C2. Instructions 781 and 181
 * seem to be related to synchronization between the CP execution and the
 * actual blank timing, but details are unknown.
 *
 *
 * Viewports
 * =========
 *
 * These specify an on-screen rectangle (a subregion of the framebuffer,
 * presumably), a projection matrix, the depth-buffer and depth queue
 * configuration, ambient lighting and clear color. The exact meaning of the
 * various fields are still partially unknown.
 *
 *
 * Modelview Matrix
 * ================
 *
 * The command stream can set each row of the modelview matrix separately. See
 * command 161.
 *
 *
 * Materials
 * =========
 *
 * It supports flat, diffuse and phong shading. XXX more to come.
 *
 *
 * Texheads
 * ========
 *
 * TODO. Textures used for 3D rendering are stored in the two available TEXRAM
 * banks. See commands 2C1, 4C1, 0C1, 0C3, 0C4.
 *
 *
 * Lights
 * ======
 *
 * According to the system16.com specs, the hardware supports 1024 lights
 * per scene, and 4 lights per polygon. It supports several light types
 * (ambient, spot, etc.) and several emission types (constant, infinite
 * linear, square, reciprocal, and reciprocal squared.)
 *
 * AFAICS, the hardware supports two light-related objects: lights and
 * lightsets. A light specifies the position, direction, emission properties
 * and more of a single light. A lightset specifies a set of (up to) four
 * lights that insist on the scene being rendered. This setup is consistent
 * with the system16 specs.
 *
 * The properties of a single light are defined by these instructions: 261,
 * 051, 961, B61, 451; instruction 104 commits the light to the GPU storage.
 * To renderer a light effective, it must be embedded in a lightset, using the
 * 064 and/or 043 instructions.
 *
 *
 * Meshes
 * ======
 *
 * This class of instructions pushes (or otherwise deals with) vertex
 * data to the transformation/rasterization pipeline.
 *
 * All meshes are specified with three primitives:
 *  - triangle strips (see command 1BC)
 *  - triangle lists (see command 1AC)
 *  - another unknown primitive (see command 12C)
 *
 * TODO. No idea where culling/vertex linking information is. Obvious
 * candidates are bits 0-4, 12-15 and 24-31 of the vertex instructions.
 */

static void
check_self_loop (hikaru_gpu_t *gpu, uint32_t target)
{
	/* XXX at some point we'll need something better than this */
	if (target == gpu->cp.pc) {
		VK_ERROR ("GPU CP: @%08X: self-jump, terminating", target);
		gpu->cp.is_running = false;
	}
}

static void
push_pc (hikaru_gpu_t *gpu)
{
	unsigned i = gpu->frame_type;
	VK_ASSERT ((gpu->cp.sp[i] >> 24) == 0x48);
	vk_buffer_put (gpu->cmdram, 4, gpu->cp.sp[i] & 0x3FFFFFF, gpu->cp.pc);
	gpu->cp.sp[i] -= 4;
}

static void
pop_pc (hikaru_gpu_t *gpu)
{
	unsigned i = gpu->frame_type;
	gpu->cp.sp[i] += 4;
	VK_ASSERT ((gpu->cp.sp[i] >> 24) == 0x48);
	gpu->cp.pc = vk_buffer_get (gpu->cmdram, 4, gpu->cp.sp[i] & 0x3FFFFFF) + 8;
}

static int
fetch (hikaru_gpu_t *gpu, uint32_t inst[8])
{
	hikaru_t *hikaru = (hikaru_t *) gpu->base.mach;
	vk_buffer_t *buffer = NULL;
	uint32_t mask = 0, i;

	/* The CS program has been observed to lie only in CMDRAM and slave
	 * RAM so far. */
	switch (gpu->cp.pc >> 24) {
	case 0x40:
	case 0x41:
		buffer = hikaru->ram_s;
		mask = 0x01FFFFFF;
		break;
	case 0x48:
	case 0x4C: /* XXX not sure */
		buffer = hikaru->cmdram;
		mask = 0x003FFFFF;
		break;
	default:
		return -1;
	}

	for (i = 0; i < 8; i++) {
		uint32_t addr = (gpu->cp.pc + i * 4) & mask;
		inst[i] = vk_buffer_get (buffer, 4, addr);
	}
	return 0;
}

static bool
is_vertex_op (uint32_t op)
{
	switch (op) {
		/* Nop */
	case 0x000:
		/* Vertex Unk */
	case 0x128 ... 0x12F:
	case 0xF28 ... 0xF2F:
		/* Vertex */
	case 0x1A8 ... 0x1AF:
	case 0xFA8 ... 0xFAF:
		/* Vertex Normal */
	case 0x1B8 ... 0x1BF:
	case 0xFB8 ... 0xFBF:
		/* Tex Coord */
	case 0xEE8:
	case 0xEE9:
		/* Tex Coord Unk */
	case 0x158 ... 0x15F:
	case 0xF58 ... 0xF5F:
		return true;
	default:
		return false;
	}
}

static float
texcoord_to_float (uint32_t x)
{
	int32_t u = (x & 0x8000) ? (x | 0xFFFF8000) : x;
	return u / 16.0f;
}

void
print_unhandled (hikaru_gpu_t *gpu, uint32_t *inst, unsigned num_words)
{
	static char info[256];
	uint32_t i, offs;
	for (i = 0, offs = 0; i < num_words; i++)
		offs += sprintf (&info[offs], "%08X ", inst[i]);
	VK_ERROR ("GPU CP: @%08X: unhandled instruction %s", gpu->cp.pc, info);
}

static inline uint32_t
exp16 (uint32_t base)
{
	return 16 << base;
}

#define I(name_) \
	static void hikaru_gpu_inst_##name_ (hikaru_gpu_t *gpu, uint32_t *inst)

/* 000	Nop
 *
 *	-------- -------- ----oooo oooooooo
 */

I (nop)
{
	VK_LOG ("GPU CP %08X: Nop [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= (inst[0] != 0);
}

/* 012	Jump [Mod: Relative]
 *
 *	-------- -------- ----mooo oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * m = Modifier: 0 = Absolute, 1 = Relative
 * A = Address or Offset in 32-bit words.
 */

I (jump)
{
	uint32_t addr;

	addr = inst[1] * 4;
	if (inst[0] & 0x800)
		addr += gpu->cp.pc;

	VK_LOG ("GPU CP %08X: Jump [%08X] %08X", gpu->cp.pc, inst[0], addr);

	check_self_loop (gpu, addr);
	gpu->cp.pc = addr;

	gpu->cp.unhandled |= ((inst[0] & 0xFFFFF7FF) != 0x12);
}

/* 052	Call [Mod: Relative]
 *
 *	-------- -------- ----mooo oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * m = Modifier: 0 = Absolute, 1 = Relative
 * A = Address or Offset in 32-bit words.
 */

I (call)
{
	uint32_t addr;

	addr = inst[1] * 4;
	if (inst[0] & 0x800)
		addr += gpu->cp.pc;

	VK_LOG ("GPU CP %08X: Call [%08X] %08X", gpu->cp.pc, inst[0], addr);

	check_self_loop (gpu, addr);
	push_pc (gpu);
	gpu->cp.pc = addr;

	gpu->cp.unhandled |= ((inst[0] & 0xFFFFF7FF) != 0x52);
}

/* 082	Return
 *
 *	-------- -------- ----oooo oooooooo
 */

I (ret)
{
	VK_LOG ("GPU CP %08X: Return [%08X]",
	        gpu->cp.pc, inst[0]);

	pop_pc (gpu);

	gpu->cp.unhandled |= (inst[0] != 0x82);
}

/* 1C2	Kill
 *
 *	-------- -------- ----oooo oooooooo
 */

I (kill)
{
	VK_LOG ("GPU CP %08X: Kill [%08X]",
	        gpu->cp.pc, inst[0]);

	gpu->cp.is_running = false;

	gpu->cp.unhandled |= (inst[0] != 0x1C2);
}

/* 781	Sync
 *
 *	-----puq -----P-Q ----oooo oooooooo
 *
 * p,q,P,Q,u = Unknown
 *
 * The values p, q, P, Q are determined by the values of ports 1A00001C and
 * 1A000020 prior to the command upload (XXX when exactly? in sync()?). Its
 * parameter is stored in (56, GBR).
 *
 * This command typically lies between the viewport/material/texhead/light
 * setup instructions at the beginning of the command stream and the
 * actual rendering commands. It _may_ act like a fence of some sorts,
 * delaying rendering until some external event (such as v-blanking) occurs.
 *
 * It may be related to 181.
 *
 * See @0C0065D6, PH:@0C016336, PH:@0C038952.
 */

I (sync)
{
	VK_LOG ("GPU CP %08X: Sync [%08X]", gpu->cp.pc, inst[0]);
}

/* 181	Set Unknown
 *
 *	-------b nnnnnnnn ----oooo oooooooo
 *
 * n = Unknown
 * b = set only if n is non-zero
 *
 * It may be related to 781.
 *
 * See PH:@0C015B50. Probably related to 781, see PH:@0C038952.
 */

I (unk_181)
{
	unsigned b = (inst[0] >> 24) & 1;
	unsigned n = (inst[0] >> 16) & 0xFF;

	VK_LOG ("GPU CP %08X: Unknown: set 181 [%08X] b=%u n=%u",
	        gpu->cp.pc, inst[0], b, n);

	gpu->cp.unhandled |= !!(inst[0] & 0xFE00F000);
}


/* 021	Viewport: Set Projection
 *
 * 	-------- -------- ----oooo oooooooo
 *	pppppppp pppppppp pppppppp pppppppp
 *	qqqqqqqq qqqqqqqq qqqqqqqq qqqqqqqq
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * p = (display height / 2) * tanf (angle / 2)
 * q = (display height / 2) * tanf (angle / 2)
 * z = depth kappa (used also in 421, 621)
 *
 * See PH:@0C01587C, PH:@0C0158A4, PH:@0C0158E8.
 */

I (vp_set_projection)
{
	gpu->viewports.scratch.persp_x		= *(float *) &inst[1];
	gpu->viewports.scratch.persp_y		= *(float *) &inst[2];
	gpu->viewports.scratch.persp_znear	= *(float *) &inst[3];

	VK_LOG ("GPU CP %08X: Viewport: Set Projection [%08X %08X %08X %08X] px=%f py=%f znear=%f",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3],
	        gpu->viewports.scratch.persp_x,
	        gpu->viewports.scratch.persp_y,
	        gpu->viewports.scratch.persp_znear);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
	gpu->cp.unhandled |= !isfinite (gpu->viewports.scratch.persp_x);
	gpu->cp.unhandled |= !isfinite (gpu->viewports.scratch.persp_y);
	gpu->cp.unhandled |= !isfinite (gpu->viewports.scratch.persp_znear);
}

/* 221	Viewport: Set Extents
 *
 *	-------- -------- ----oooo oooooooo
 *	jjjjjjjj jjjjjjjj cccccccc cccccccc
 *	--YYYYYY YYYYYYYY -XXXXXXX XXXXXXXX
 *	--yyyyyy yyyyyyyy -xxxxxxx xxxxxxxx
 *
 * c,j = center (X,Y) coordinates
 * x,y = extent (X,Y) minima
 * X,Y = extent (X,Y) maxima
 *
 * NOTE: according to the code, X can be at most 640, Y can be at most 512;
 * and at least one of x and y must be zero.
 *
 * See PH:@0C015924
 */

I (vp_set_extents)
{
	vec2s_t center, extents_x, extents_y;

	center[0]	= inst[1] & 0xFFFF;
	center[1]	= inst[1] >> 16;
	extents_x[0]	= inst[2] & 0x7FFF;
	extents_x[1]	= inst[3] & 0x7FFF;
	extents_y[0]	= (inst[2] >> 16) & 0x3FFF;
	extents_y[1]	= (inst[3] >> 16) & 0x3FFF;

	gpu->viewports.scratch.center[0] = center[0];
	gpu->viewports.scratch.center[1] = center[1];
	gpu->viewports.scratch.extents_x[0] = extents_x[0];
	gpu->viewports.scratch.extents_x[1] = extents_x[1];
	gpu->viewports.scratch.extents_y[0] = extents_y[0];
	gpu->viewports.scratch.extents_y[1] = extents_y[1];

	VK_LOG ("GPU CP %08X: Viewport: Set Extents [%08X %08X %08X %08X] center=<%u,%u> x=<%u,%u> y=<%u,%u> ]",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3],
	        center[0], center[1],
	        extents_x[0], extents_x[1],
	        extents_y[0], extents_y[1])

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
	gpu->cp.unhandled |= !!(inst[2] & 0xC0008000);
	gpu->cp.unhandled |= !!(inst[3] & 0xC0008000);
	gpu->cp.unhandled |= (extents_x[1] > 640 ||
	                      extents_y[1] > 512);
	gpu->cp.unhandled |= (center[0] < extents_x[0] ||
	                      center[0] > extents_x[1]);
	gpu->cp.unhandled |= (center[1] < extents_y[0] ||
	                      center[1] > extents_y[1]);
}

/* 421	Viewport: Set Depth Test/Range
 *
 *	-------- -------- ----oooo oooooooo
 *	nnnnnnnn nnnnnnnn nnnnnnnn nnnnnnnn
 *	ffffffff ffffffff ffffffff ffffffff
 *	FFF----- -------- -------- --------
 *
 * n = depth near; used also in 021 and 621
 * f = depth far
 * F = depth test function?
 *
 * See PH:@0C015AA6
 */

I (vp_set_depth)
{
	gpu->viewports.scratch.depth_near = *(float *) &inst[1];
	gpu->viewports.scratch.depth_far  = *(float *) &inst[2];
	gpu->viewports.scratch.depth_func = inst[3] >> 29;

	VK_LOG ("GPU CP %08X: Viewport: Set Depth Range [%08X %08X %08X %08X] func=%u near=%f far=%f",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3],
	        gpu->viewports.scratch.depth_func,
	        gpu->viewports.scratch.depth_near,
	        gpu->viewports.scratch.depth_far);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
	gpu->cp.unhandled |= !!(inst[3] & 0x1FFFFFFF);
	gpu->cp.unhandled |= !isfinite (gpu->viewports.scratch.depth_near) ||
	                     !isfinite (gpu->viewports.scratch.depth_far);
	gpu->cp.unhandled |= gpu->viewports.scratch.depth_near >=
	                     gpu->viewports.scratch.depth_far;
	gpu->cp.unhandled |= (gpu->viewports.scratch.depth_near < 0.0f) ||
	                     (gpu->viewports.scratch.depth_far < 0.0f);
}

/* 621	Viewport: Set Depth Queue
 *
 *	-------- ----ttDu ----oooo oooooooo
 *	AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
 *	dddddddd dddddddd dddddddd dddddddd 
 *	gggggggg gggggggg gggggggg gggggggg
 *
 * t = depth queue type (function)
 * D = disable depth queue?
 * u = Unknown
 * RGBA = color/mask?
 * f = depth density [1]
 * g = depth bias [2]
 *
 * [1] Computed as 1.0f (constant), 1.0f / zdelta, or
 *     1.0f / sqrt (zdelta**2); where zdelta = zend - zstart.
 *
 * [2] Computed as depth_near / depth_far.
 *
 * XXX I'm not attaching any particular meaning to 'density' and 'bias' here;
 * they are just placeholders.
 *
 * See PH:@0C0159C4, PH:@0C015A02, PH:@0C015A3E.
 */

I (vp_set_depth_queue)
{
	gpu->viewports.scratch.depthq_type	= (inst[0] >> 18) & 3;
	gpu->viewports.scratch.depthq_enabled	= ((inst[0] >> 17) & 1) ? 0 : 1;
	gpu->viewports.scratch.depthq_unk	= (inst[0] >> 16) & 1;
	gpu->viewports.scratch.depthq_mask[0]	= inst[1] & 0xFF;
	gpu->viewports.scratch.depthq_mask[1]	= (inst[1] >> 8) & 0xFF;
	gpu->viewports.scratch.depthq_mask[2]	= (inst[1] >> 16) & 0xFF;
	gpu->viewports.scratch.depthq_mask[3]	= inst[1] >> 24;
	gpu->viewports.scratch.depthq_density	= *(float *) &inst[2];
	gpu->viewports.scratch.depthq_bias	= *(float *) &inst[3];

	VK_LOG ("GPU CP %08X: Viewport: Set Depth Queue [%08X %08X %08X %08X] type=%u enabled=%u unk=%u mask=<%X %X %X %X> density=%f bias=%f",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3],
		gpu->viewports.scratch.depthq_type,
		gpu->viewports.scratch.depthq_enabled,
		gpu->viewports.scratch.depthq_unk,
		gpu->viewports.scratch.depthq_mask[0],
		gpu->viewports.scratch.depthq_mask[1],
		gpu->viewports.scratch.depthq_mask[2],
		gpu->viewports.scratch.depthq_mask[3],
		gpu->viewports.scratch.depthq_density,
		gpu->viewports.scratch.depthq_bias);
}

/* 811	Viewport: Set Ambient Color
 *
 *	rrrrrrrr rrrrrrrr ----oooo oooooooo
 *	bbbbbbbb bbbbbbbb gggggggg gggggggg
 *
 * r,g,b = color
 *
 * See PH:@0C037840.
 */

I (vp_set_ambient_color)
{
	vec3s_t color;

	color[0] = inst[0] >> 16;
	color[1] = inst[1] & 0xFFFF;
	color[2] = inst[1] >> 16;

	gpu->viewports.scratch.ambient_color[0] = color[0];
	gpu->viewports.scratch.ambient_color[1] = color[1];
	gpu->viewports.scratch.ambient_color[2] = color[2];

	VK_LOG ("GPU CP %08X: Viewport: Set Ambient Color [%08X %08X] color=<%u %u %u>",
	        gpu->cp.pc, inst[0], inst[1],
	        color[2], color[1], color[0]);
}

/* 991	Viewport: Set Clear Color
 *
 *	-------- -------- ----oooo oooooooo
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

I (vp_set_clear_color)
{
	vec4b_t color;

	color[0] = inst[1] & 0xFF;
	color[1] = (inst[1] >> 8) & 0xFF;
	color[2] = (inst[1] >> 16) & 0xFF;
	color[3] = ((inst[1] >> 24) & 1) ? 0xFF : 0;

	gpu->viewports.scratch.clear_color[0] = color[0];
	gpu->viewports.scratch.clear_color[1] = color[1];
	gpu->viewports.scratch.clear_color[2] = color[2];
	gpu->viewports.scratch.clear_color[3] = color[3];

	VK_LOG ("GPU CP %08X: Viewport: Set Clear Color [%08X %08X] color=<%u %u %u %u>",
	        gpu->cp.pc, inst[0], inst[1],
	        color[0], color[1], color[2], color[3]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
	gpu->cp.unhandled |= !!(inst[0] & 0xFE000000);
}

/* 004	Commit Viewport
 *
 *	-------- -----iii ----oooo oooooooo
 *
 * i = Index
 *
 * See PH:@0C015AD0.
 */

I (vp_commit)
{
	uint32_t index = inst[0] >> 16;

	VK_LOG ("GPU CP %08X: Commit Viewport #%u [%08X]",
	        gpu->cp.pc, index, inst[0]);

	if (index >= NUM_VIEWPORTS) {
		VK_ERROR ("GPU CP: viewport commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_VIEWPORTS);
		gpu->cp.unhandled |= true;
		return;
	}

	gpu->viewports.table[index] = gpu->viewports.scratch;
	gpu->viewports.table[index].set = true;

	gpu->cp.unhandled |= !!(inst[0] & 0xFFF8F000);
}

/* 003	Recall Viewport
 *
 *	-------- -----iii -xx-oooo oooooooo
 *
 * i = Index
 * x = Unknown (2003 and 4003 variants are used in BRAVEFF title screen)
 *
 * See PH:@0C015AF6, PH:@0C015B12, PH:@0C015B32.
 */

I (vp_recall)
{
	uint32_t index = inst[0] >> 16;

	VK_LOG ("GPU CP %08X: Recall Viewport #%u [%08X]",
	        gpu->cp.pc, index, inst[0]);

	if (index >= NUM_VIEWPORTS) {
		VK_ERROR ("GPU CP: viewport recall index exceeds MAX (%u >= %u), skipping",
		          index, NUM_VIEWPORTS);
		gpu->cp.unhandled |= true;
		return;
	}

	if (!gpu->viewports.table[index].set) {
		VK_ERROR ("GPU CP: recalled viewport was not set (%u), skipping",
		          index);
		gpu->cp.unhandled |= true;
		return;
	}

	gpu->viewports.table[index].used = true;
	hikaru_renderer_set_viewport (gpu->renderer,
	                              &gpu->viewports.table[index]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFF89000);
}

/* 161	Set Matrix Vector
 *
 *	-------- ----mmnn ----oooo oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * m = Unknown
 * n = Vector index
 * x,y,z = Vector elements
 *
 * This command sets a row vector of the current modelview matrix. Typically
 * four of these commands follow and set the whole 4x3 modelview matrix;
 * however these commands can be used to update just one row at a time.
 *
 * See @0C008080.
 */

I (modelview_set_vector)
{
	unsigned m = (inst[0] >> 18) & 3;
	unsigned n = (inst[0] >> 16) & 3;
	vec3f_t v;

	v[0] = *(float *) &inst[1];
	v[1] = *(float *) &inst[2];
	v[2] = *(float *) &inst[3];

	VK_LOG ("GPU CP %08X: Matrix: Vector [%08X %08X %08X %08X] %u %u <%f %f %f>",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3],
	        n, m, v[0], v[1], v[2]);

	hikaru_renderer_set_modelview_vector (gpu->renderer, n, m, v);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFF0F000);
	gpu->cp.unhandled |= !isfinite (v[0]);
	gpu->cp.unhandled |= !isfinite (v[1]);
	gpu->cp.unhandled |= !isfinite (v[2]);
}

/* 091	Material: Set Color 0
 * 291	Material: Set Color 1
 *
 *	-------- -------- ----oooo oooooooo
 *	uuuuuuuu bbbbbbbb gggggggg rrrrrrrr
 *
 * r,g,b = RGB color
 * u = set but unused? Seen in BRAVEFF title screen.
 *
 * See PH:@0C0CF742.
 */

I (mat_set_color_0_1)
{
	uint32_t i = (inst[0] >> 9) & 1;

	gpu->materials.scratch.color[i][0] = inst[1] & 0xFF;
	gpu->materials.scratch.color[i][1] = (inst[1] >> 8) & 0xFF;
	gpu->materials.scratch.color[i][2] = (inst[1] >> 16) & 0xFF;

	VK_LOG ("GPU CP %08X: Material: Set Color %X [%08X %08X] <R=%u G=%u B=%u>",
	        gpu->cp.pc, i, inst[0], inst[1],
	        gpu->materials.scratch.color[i][0],
	        gpu->materials.scratch.color[i][1],
	        gpu->materials.scratch.color[i][2]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
}

/* 491	Material: Set Shininess
 *
 *	-------- -------- ----oooo oooooooo
 *	ssssssss bbbbbbbb gggggggg rrrrrrrr
 *
 * s = Specular shininess
 *
 * See PH:@0C0CF798, PH:@0C01782C.
 */

I (mat_set_shininess)
{
	uint8_t specularity;
	vec3b_t shininess;

	shininess[0] = inst[1] & 0xFF;
	shininess[1] = (inst[1] >> 8) & 0xFF;
	shininess[2] = (inst[1] >> 16) & 0xFF;

	specularity = inst[1] >> 24;

	gpu->materials.scratch.shininess[0] = shininess[0];
	gpu->materials.scratch.shininess[1] = shininess[1];
	gpu->materials.scratch.shininess[2] = shininess[2];
	gpu->materials.scratch.specularity = specularity;

	VK_LOG ("GPU CP %08X: Material: Set Shininess [%08X %08X] specularity=%u shininess=<%u %u %u>",
	        gpu->cp.pc, inst[0], inst[1],
	        specularity, shininess[2],
	        shininess[1], shininess[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
}

/* 691	Material: Set Material Color
 *
 *	rrrrrrrr rrrrrrrr ----oooo oooooooo
 *	bbbbbbbb bbbbbbbb gggggggg gggggggg
 *
 * See PH:@0C0CF7CC.
 */

I (mat_set_material_color)
{
	vec3s_t color;

	color[0] = inst[0] >> 16;
	color[1] = inst[1] & 0xFFFF;
	color[2] = inst[1] >> 16;

	gpu->materials.scratch.material_color[0] = color[0];
	gpu->materials.scratch.material_color[1] = color[1];
	gpu->materials.scratch.material_color[2] = color[2];

	VK_LOG ("GPU CP %08X: Material: Set Material Color [%08X %08X] color=<%u %u %u>",
	        gpu->cp.pc, inst[0], inst[1],
	        color[0], color[1], color[2]);

	gpu->cp.unhandled |= !!(inst[0] & 0x0000F000);
}

/* 081	Material: Set Unknown
 *
 *	-------- ----mmmm ---noooo oooooooo
 *
 * n, m = Unknown
 */

I (mat_set_unk_081)
{
	unsigned n = (inst[0] >> 12) & 1;
	unsigned m = (inst[0] >> 16) & 0xF;

	VK_LOG ("GPU CP %08X: Material: Set Unknown [%08X] %u %u",
	        gpu->cp.pc, inst[0], n, m);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFF0E000);
}

/* 881	Material: Set Flags
 *
 *	-------- --hatzSS ----oooo oooooooo
 *
 * S = Shading mode
 * z = Depth blend (fog)
 * t = Enable texture
 * a = Alpha mode
 * h = Highlight mode
 *
 * Shading mode should include Flat, Linear, Phong.
 *
 * See PH:@0C0CF700.
 */

I (mat_set_flags)
{
	gpu->materials.scratch.shading_mode	= (inst[0] >> 16) & 3;
	gpu->materials.scratch.depth_blend	= (inst[0] >> 18) & 1;
	gpu->materials.scratch.has_texture	= (inst[0] >> 19) & 1;
	gpu->materials.scratch.has_alpha	= (inst[0] >> 20) & 1;
	gpu->materials.scratch.has_highlight	= (inst[0] >> 21) & 1;

	VK_LOG ("GPU CP %08X: Material: Set Flags [%08X] mode=%u zblend=%u tex=%u alpha=%u highl=%u",
	        gpu->cp.pc, inst[0],
		gpu->materials.scratch.shading_mode,
		gpu->materials.scratch.depth_blend,
		gpu->materials.scratch.has_texture,
		gpu->materials.scratch.has_alpha,
		gpu->materials.scratch.has_highlight);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFC0F000);
}

/* A81	Material: Set Blending Mode
 *
 *	-------- -------mm ----oooo oooooooo
 *
 * m = Blending Mode
 *
 * See PH:@0C0CF7FA.
 */

I (mat_set_blending)
{
	gpu->materials.scratch.blending_mode = (inst[0] >> 16) & 3;

	VK_LOG ("GPU CP %08X: Material: Set Blending Mode [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFCF000);
}

/* C81	Material: Set Unknown
 *
 *	-------- --xuuuuu ----oooo oooooooo
 *
 * x,u = Unknown
 *
 * It may be related to command 154, see PH:@0C0CF872 and
 * PH:@0C0CF872.
 */

I (mat_set_unk_C81)
{
	VK_LOG ("GPU CP %08X: Material: Set C81 [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFC0F000);
}

/* 084	Commit Material
 *
 *	------nn nnnnnnnn ---uoooo oooooooo
 *
 * n = Index
 * u = Unknown; always 1?
 *
 * See PH:@0C0153D4, PH:@0C0CF878.
 */

I (mat_commit)
{
	uint32_t offset  = inst[0] >> 16;
	uint32_t index   = offset + gpu->materials.base;

	VK_LOG ("GPU CP %08X: Commit Material #%u (%u) [%08X]",
	        gpu->cp.pc, index, gpu->materials.base, inst[0]);

	if (index >= NUM_MATERIALS) {
		VK_ERROR ("GPU CP: material commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_MATERIALS);
		return;
	}

	gpu->materials.table[index] = gpu->materials.scratch;
	gpu->materials.table[index].set = true;

	gpu->cp.unhandled |= !!(inst[0] & 0xFC00E000);
	gpu->cp.unhandled |= ((inst[0] & 0x1000) != 0x1000);
	gpu->cp.unhandled |= (offset > 0x3FF);
}

/* 083	Recall Material
 *
 *	nnnnnnnn nnnnnnnn ---Moooo oooooooo
 *
 * n = Index
 * M = Modifier: 0 = set base only, 1 = recall for real
 *
 * See @0C00657C, PH:@0C0CF882.
 *
 * XXX n here is likely too large.
 */

I (mat_recall)
{
	uint32_t offset = inst[0] >> 16;
	uint32_t make_active = (inst[0] >> 12) & 1;
	uint32_t index = gpu->materials.base + offset;

	VK_LOG ("GPU CP %08X: %s #%u (%u) [%08X]",
	        gpu->cp.pc, make_active ? "Recall Material" : "Set Material Offs",
	        index, gpu->materials.base, inst[0]);

	if (!make_active)
		gpu->materials.base = offset;
	else {
		if (index >= NUM_MATERIALS) {
			VK_ERROR ("GPU CP: material recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_MATERIALS);
			gpu->cp.unhandled |= true;
			return;
		}
		if (!gpu->materials.table[index].set) {
			VK_ERROR ("GPU CP: recalled material was not set (%u), skipping",
			          index);
			gpu->cp.unhandled |= true;
			return;
		}
		hikaru_renderer_set_material (gpu->renderer,
		                              &gpu->materials.table[index]);
	}

	gpu->cp.unhandled |= !!(inst[0] & 0x0000E000);
}

/* 0C1	Texhead: Set Unknown
 *
 *	----mmmm mmmmnnnn ----oooo oooooooo
 *
 * m, n = Unknown
 *
 * See PH:@0C015B7A.
 */

I (tex_set_unk_0C1)
{
	gpu->texheads.scratch._0C1_nibble	= (inst[0] >> 16) & 0xF;
	gpu->texheads.scratch._0C1_byte		= (inst[0] >> 20) & 0xFF;

	VK_LOG ("GPU CP %08X: Texhead: Set Unknown [%08X]",
	        gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xF000F000);
}

/* 2C1	Texhead: Set Format/Size
 *
 *	888FFFll llHHHWWW uu--oooo oooooooo
 *
 * 8 = Unknown		[argument on stack]
 * F = Format		[argument R7]
 * H = log16 of Height	[argument R6]
 * l = Unknown		[lower four bits of argument R4]
 * W = log16 of Width	[argument R5]
 * u = Unknown		[upper two bits of argument R4]
 *
 * See PH:@0C015BCC.
 */

I (tex_set_format)
{
	gpu->texheads.scratch.width	= exp16 ((inst[0] >> 16) & 7);
	gpu->texheads.scratch.height	= exp16 ((inst[0] >> 19) & 7);
	gpu->texheads.scratch.format	= (inst[0] >> 26) & 7;
	gpu->texheads.scratch.log_width	= (inst[0] >> 16) & 7;
	gpu->texheads.scratch.log_height= (inst[0] >> 19) & 7;

	if (gpu->texheads.scratch.format == HIKARU_FORMAT_RGBA1111)
		gpu->texheads.scratch.width *= 2; /* pixels per word */

	gpu->texheads.scratch._2C1_unk4	= (((inst[0] >> 14) & 3) << 4) |
	                                  ((inst[0] >> 22) & 15);
	gpu->texheads.scratch._2C1_unk8	= inst[0] >> 29;

	VK_LOG ("GPU CP %08X: Texhead: Set Format/Size [%08X]",
	        gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0x00003000);
}

/* 4C1	Texhead: Set Slot
 *
 *	nnnnnnnn mmmmmmmm ---boooo oooooooo
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

I (tex_set_slot)
{
	gpu->texheads.scratch.bank  = (inst[0] >> 12) & 1;
	gpu->texheads.scratch.slotx = (inst[0] >> 16) & 0xFF;
	gpu->texheads.scratch.sloty = inst[0] >> 24;

	VK_LOG ("GPU CP %08X: Texhead: Set Slot [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0x0000E000);
}

/* 0C4	Commit Texhead
 *
 *	-----nn nnnnnnnn ---uoooo oooooooo
 *
 * n = Index
 * u = Unknown; always 1?
 *
 * See PH:@0C01545C. 
 */

I (tex_commit)
{
	uint32_t offset  = inst[0] >> 16;
	uint32_t index   = offset + gpu->texheads.base;

	VK_LOG ("GPU CP %08X: Commit Texhead #%u (%u) [%08X]",
	        gpu->cp.pc, index, gpu->texheads.base, inst[0]);

	if (index >= NUM_TEXHEADS) {
		VK_ERROR ("GPU CP: texhead commit index exceedes MAX (%u >= %u), skipping",
		          index, NUM_MATERIALS);
		return;
	}

	gpu->texheads.table[index] = gpu->texheads.scratch;
	gpu->texheads.table[index].set = true;

	gpu->cp.unhandled |= !!(inst[0] & 0xFC00E000);
	gpu->cp.unhandled |= ((inst[0] & 0x1000) != 0x1000);
}

/* 0C3	Recall Texhead
 *
 *	nnnnnnnn nnnnnnnn ---Moooo oooooooo
 *
 * n = Index
 * M = Modifier: 0 = set base only, 1 = recall for real
 *
 * XXX n here is likely too large.
 */

I (tex_recall)
{
	uint32_t offset = inst[0] >> 16;
	uint32_t make_active = (inst[0] >> 12) & 1;
	uint32_t index = gpu->texheads.base + offset;

	VK_LOG ("GPU CP %08X: %s #%u (%u) [%08X]",
	        gpu->cp.pc, make_active ? "Recall Texhead" : "Set Texhead Offs",
	        index, gpu->texheads.base, inst[0]);

	if (!make_active)
		gpu->texheads.base = offset;
	else {
		if (index >= NUM_TEXHEADS) {
			VK_ERROR ("GPU CP: texhead recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_TEXHEADS);
			gpu->cp.unhandled |= true;
			return;
		}
		if (!gpu->texheads.table[index].set) {
			VK_ERROR ("GPU CP: recalled texhead was not set (%u), skipping",
			          index);
			gpu->cp.unhandled |= true;
			return;
		}
		gpu->texheads.table[index].used = true;
		hikaru_renderer_set_texhead (gpu->renderer,
		                             &gpu->texheads.table[index]);
	}

	gpu->cp.unhandled |= !!(inst[0] & 0x0000E000);
}

/* 261	Set Light Type/Unknown
 *
 *	-------- ------tt ----oooo oooooooo
 *	pppppppp pppppppp pppppppp pppppppp
 *	qqqqqqqq qqqqqqqq qqqqqqqq qqqqqqqq
 *	-------- -------- -------- --------
 *
 * t = Light type
 * p = Unknown \ power/emission/exponent/dacay/XXX
 * q = Unknown /
 *
 * Type 0:
 *
 *  p = 1.0f or 1.0f / (FR4 - FR5)
 *  q = 1.0f or -FR5
 *
 * NOTE:
 *  - The first variant sets (16,GBR) to +INF and (17,GBR) to +INF, see
 *    PH:@0C0178FC.
 *  - The second variant sets (16,GBR) to FR5 and (17,GBR) to FR5**2, see
 *    PH:@0C017934.
 *
 * Type 1:
 *
 *  p = 1.0f / (FR4**2 - FR5**2)
 *  q = -FR5**2
 *
 * NOTE: it sets (16,GBR) to X and (17,GBR) to Y.
 *
 * Type 2:
 *
 *  p = (FR4 * FR5) / (FR4 - FR5)
 *  q = 1.0f / |FR5|
 *
 * NOTE: it sets (16,GBR) to X and (17,GBR) to Y.
 *
 * Type 3:
 *
 *  p = (FR4**2 * FR5**2) / (FR5**2 - FR4**2)
 *  q = 1.0 / |FR5**2|
 *
 * NOTE: it sets (16,GBR) to X and (17,GBR) to Y.
 *
 * NOTE: light types according to the PHARRIER text are: constant, infinite,
 * square, reciprocal, reciprocal2, linear.
 */

I (lit_set_type)
{
	gpu->lights.scratch.emission_type	= (inst[0] >> 16) & 3;
	gpu->lights.scratch.emission_p		= *(float *) &inst[1];
	gpu->lights.scratch.emission_q		= *(float *) &inst[2];

	VK_LOG ("GPU CP %08X: Light: Set Type/Extents [%08X %08X %08X %08X] type=%u p=%f q=%f",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3],
	        gpu->lights.scratch.emission_type,
	        gpu->lights.scratch.emission_p,
	        gpu->lights.scratch.emission_q);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFCF000);
}

/*
 * 961	Set Light Position
 *
 *	-------- -------e nnnnoooo oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * e = Unknown
 * n = Unknown
 * x,y,z = position
 *
 * Variants include 16961, 10961, 8961. Apparently the 8961 variant makes use
 * of the 194 ramp data.
 *
 * NOTE: the position can be (NaN,NaN,NaN = (Inf,Inf,Inf)?
 */

I (lit_set_position)
{
	gpu->lights.scratch.position[0] = *(float *) &inst[1];
	gpu->lights.scratch.position[1] = *(float *) &inst[2];
	gpu->lights.scratch.position[2] = *(float *) &inst[3];

	VK_LOG ("GPU CP %08X: Light: Set Position [%08X %08X %08X %08X] <%f %f %f>",
	        gpu->cp.pc,
	        inst[0], inst[1], inst[2], inst[3],
	        gpu->lights.scratch.position[0],
	        gpu->lights.scratch.position[1],
	        gpu->lights.scratch.position[2]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFE0000);
}

/*
 * B61	Set Light Direction
 *
 *	-------- -------- nnnnoooo oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * n = Unknown
 * x,y,z = direction
 *
 * Variants include 8B61.
 *
 * NOTE: the direction can be (0,0,0) or (NaN,NaN,NaN) = (Inf,Inf,Inf)?
 */

I (lit_set_direction)
{
	gpu->lights.scratch.direction[0] = *(float *) &inst[1];
	gpu->lights.scratch.direction[1] = *(float *) &inst[2];
	gpu->lights.scratch.direction[2] = *(float *) &inst[3];

	VK_LOG ("GPU CP %08X: Light: Set Direction [%08X %08X %08X %08X] <%f %f %f>",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3],
	        gpu->lights.scratch.direction[0],
	        gpu->lights.scratch.direction[1],
	        gpu->lights.scratch.direction[2]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFF0000);
}

/* 051	Light: Set Unknown (Color? Ramp?)
 *
 *	-------- nnnnnnnn ----oooo oooooooo
 *	--aaaaaa aaaabbbb bbbbbbcc cccccccc
 *
 * n = Index? Index into the 194 ramp data?
 * a,b,c = Color? = FP * 255.0f and then truncated and clamped to [0,FF].
 *
 * See PH:@0C0178C6; for a,b,c computation see PH:@0C03DC66.
 */

I (lit_set_unk_051)
{
	vec3s_t color;
	uint32_t index;

	index = (inst[0] >> 16) & 0xFF;
	color[0] = inst[1] & 0x3FF;
	color[1] = (inst[1] >> 10) & 0x3FF;
	color[2] = (inst[1] >> 20) & 0x3FF;

	VK_LOG ("GPU CP %08X: Light: Set Color [%08X %08X] %u <%u %u %u>",
	        gpu->cp.pc, inst[0], inst[1],
	        index, color[2], color[1], color[0]);

	gpu->lights.scratch._051_index = index;
	gpu->lights.scratch._051_color[0] = color[0];
	gpu->lights.scratch._051_color[1] = color[1];
	gpu->lights.scratch._051_color[2] = color[2];

	gpu->cp.unhandled |= !!(inst[0] & 0xFF00F000);
	gpu->cp.unhandled |= !!(inst[1] & 0xC0000000);
}

/* 451	Light: Set Unknown
 *
 *	-------u -------- ----oooo oooooooo
 *	-------- -------- -------- --------
 *
 * u = Unknown
 *
 * See PH:@0C017A7C, PH:@0C017B6C, PH:@0C017C58,
 * PH:@0C017CD4, PH:@0C017D64, 
 */

I (lit_set_unk_451)
{
	VK_LOG ("GPU CP %08X: Light: Set Unknown 451 [%08X %08X]",
	        gpu->cp.pc, inst[0], inst[1]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFEFFF000);
}

/* 561	Light: Set Unknown
 *
 *	-------- ------nn ----oooo oooooooo
 *	-------- -------- -------- --------
 *	-------- -------- -------- --------
 *	-------- -------- -------- --------
 */

I (lit_set_unk_561)
{
	VK_LOG ("GPU CP %08X: Light: Set Unknown 561 [%08X %08X %08X %08X]",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFCF000);
}

/* 006	Light: Unknown
 *
 *	-------- -------- ----oooo oooooooo
 */

I (lit_unk_006)
{
	VK_LOG ("GPU CP %08X: Light: Unknown 006 [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
}

/* 046	Light: Unknown
 *
 *	-------- -------n ----oooo oooooooo
 *
 * n = Unknown
 */

I (lit_unk_046)
{
	VK_LOG ("GPU CP %08X: Light: Unknown 046 [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFEF000);
}

/* 104	Commit Light
 *
 *	------nn nnnnnnnn ----oooo oooooooo
 *
 * n = Index
 */

I (lit_commit)
{
	unsigned index = inst[0] >> 16;

	VK_LOG ("GPU CP %08X: Commit Light #%u [%08X]",
	        gpu->cp.pc, index, inst[0]);

	if (index >= NUM_LIGHTS) {
		VK_ERROR ("GPU CP: light commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_LIGHTS);
		return;
	}

	gpu->lights.table[index] = gpu->lights.scratch;
	gpu->lights.table[index].set = true;

	gpu->cp.unhandled |= !!(inst[0] & 0xFC00F000);
}

/* 064  Commit Lightset
 *
 *      -------- nnnnnnnn ---Moooo oooooooo
 *      bbbbbbbb bbbbbbbb aaaaaaaa aaaaaaaa
 *      dddddddd dddddddd cccccccc cccccccc
 *      -------- -------- -------- --------
 *
 * M = Unknown
 * n = Index
 * a,b,c,d = indices of four light vectors
 *
 * See PH:@0C017DF0.
 */

I (lit_commit_set)
{
	uint32_t offset = (inst[0] >> 16) & 0xFF;
	uint32_t light0 = inst[1] & 0xFFFF;
	uint32_t light1 = inst[1] >> 16;
	uint32_t light2 = inst[2] & 0xFFFF;
	uint32_t light3 = inst[2] >> 16;
	uint32_t index = offset + gpu->lights.base;

	VK_LOG ("GPU CP %08X: Commit Lightset #%u (%u) [%08X %08X %08X %08X]",
	        gpu->cp.pc, index, gpu->lights.base, inst[0], inst[1], inst[2], inst[3]);

	if (index >= NUM_LIGHTSETS) {
		VK_ERROR ("GPU CP: lightset commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_LIGHTSETS);
		return;
	}
	if (!gpu->lights.table[light0].set ||
	    !gpu->lights.table[light1].set ||
	    !gpu->lights.table[light2].set ||
	    !gpu->lights.table[light3].set) {
		VK_ERROR ("GPU CP: lightset commit includes unset lights (%u,%u,%u,%u), skipping",
		          light0, light1, light2, light3);
		return;
	}

	gpu->lights.sets[index].lights[0] = &gpu->lights.table[light0];
	gpu->lights.sets[index].lights[1] = &gpu->lights.table[light1];
	gpu->lights.sets[index].lights[2] = &gpu->lights.table[light2];
	gpu->lights.sets[index].lights[3] = &gpu->lights.table[light3];
	gpu->lights.sets[index].set = true;

	gpu->cp.unhandled |= !!(inst[0] & 0xFF00E000);
	/* XXX used by the BOOTROM; possibly disables lighting */
	gpu->cp.unhandled |= ((inst[0] & 0x1000) != 0x1000);
}

/* 043	Recall Lightset
 *
 *	----EEEE NNNNNNNN ---Moooo oooooooo
 *
 * M = Unknown (always set?)
 * E = Bitmask specifying which lights in the set are enabled?
 * n = Index
 *
 * There doesn't seem like to be any base/offset mechanics for lightsets.
 */

I (lit_recall_set)
{
	uint32_t make_active = (inst[0] >> 12) & 1;
	uint32_t offset = (inst[0] >> 16) & 0xFF;
	uint32_t enabled_mask = (inst[0] >> 24) & 0xF;
	uint32_t index = gpu->lights.base + offset;

	VK_LOG ("GPU CP %08X: %s #%u (%u) [%08X]",
	        gpu->cp.pc, make_active ? "Recall Lightset" : "Set Lightset Offs",
	        index, gpu->lights.base, inst[0]);

	if (!make_active)
		gpu->lights.base = offset;
	else {
		if (index >= NUM_LIGHTSETS) {
			VK_ERROR ("GPU CP: lightset recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_LIGHTSETS);
			gpu->cp.unhandled |= true;
			return;
		}
		if (!gpu->lights.sets[index].set) {
			VK_ERROR ("GPU CP: recalled lightset was not set (%u), skipping",
			          index);
			gpu->cp.unhandled |= true;
			return;
		}
		gpu->lights.sets[index].used = true;
		hikaru_renderer_set_lightset (gpu->renderer,
		                              &gpu->lights.sets[index],
		                              enabled_mask);
	}

	gpu->cp.unhandled |= !!(inst[0] & 0xF000E000);
}

/* 1AC	Vertex
 *
 *	wwwwwwww -------- jjjjoooo oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * x,y,z = Vertex position
 */

I (mesh_add_position)
{
	vec3f_t pos;

	pos[0] = *(float *) &inst[1];
	pos[1] = *(float *) &inst[2];
	pos[2] = *(float *) &inst[3];

	VK_LOG ("GPU CP %08X: Vertex [%08X] { %f %f %f }",
	        gpu->cp.pc, inst[0],
	        pos[0], pos[1], pos[2]);

	hikaru_renderer_add_vertex (gpu->renderer, pos);

	gpu->cp.unhandled |= !!(inst[0] & 0x00FF0000);
}

/* 12C	Vertex Unk [Fixed-Point Triangle]
 *
 *	wwwwwwww u------- jjjjoooo oooobbbb
 *	???????? ???????? ???????? ????????
 *	???????? ???????? ???????? ????????
 *	???????? ???????? ???????? ????????
 *
 * j,u,w = Unknown
 *
 * The last three words contain vertex data in some (fixed point?) format,
 * for three vertices. The format looks like 10/10/10 or something. There's
 * also sign bits involved.
 */

I (mesh_add_position_unk)
{
	VK_LOG ("GPU CP %08X: Vertex Unk [%08X %08X %08X %08X]",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3]);

	gpu->cp.unhandled |= !!(inst[0] & 0x007F0000);
}

/* 1BC  Vertex Normal
 *
 *	pppppppp mmmmnnnn qqqqoooo oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *	ssssssss ssssssss tttttttt tttttttt
 *	uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvvv
 *	wwwwwwww wwwwwwww wwwwwwww wwwwwwww
 *
 * x,y,z = Vertex position
 * u,v,w = Vertex normal
 * s,t = Vertex texcoords
 * n,m,p,q = Unknown
 * 1 vs F = Unknown
 * 8 vs 9 vs ... vs F = Unknown (vertex linking/winding info?)
 */

I (mesh_add_position_normal)
{
	unsigned n, m, p, q;
	vec3f_t pos, nrm;
	vec2f_t texcoords;

	p = inst[0] >> 24;
	n = (inst[0] >> 20) & 15;
	m = (inst[0] >> 16) & 15;
	q = (inst[0] >> 12) & 15;

	pos[0] = *(float *) &inst[1];
	pos[1] = *(float *) &inst[2];
	pos[2] = *(float *) &inst[3];

	nrm[0] = *(float *) &inst[5];
	nrm[1] = *(float *) &inst[6];
	nrm[2] = *(float *) &inst[7];

	texcoords[0] = texcoord_to_float (inst[4] & 0xFFFF);
	texcoords[1] = texcoord_to_float (inst[4] >> 16);

	VK_LOG ("GPU CP %08X: Vertex Normal [%08X %08X %08X %08X %08X %08X %08X %08X] <%f %f %f> <%f %f %f> <%f %f> %u %u %u %u",
	        gpu->cp.pc,
		inst[0], inst[1], inst[2], inst[3],
		inst[4], inst[5], inst[6], inst[7],
	        pos[0], pos[1], pos[2],
	        nrm[0], nrm[1], nrm[2],
		texcoords[0], texcoords[1],
	        n, m, p, q);

	hikaru_renderer_add_vertex_full (gpu->renderer,
	                                 pos, nrm, texcoords);
}

/* EE8	Tex Coord
 *
 *	-------- -------- ----oooo oooooooC
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *
 * u,v = Texture coordinates
 * C = Selects CW/CCW winding?
 *
 * This command specifies the texture coordinates for a triangle. The vertex
 * positions are determined by the preceding 'Vertex' instructions. The nth
 * word here specifies the tex coords for the (current_vertex_index - n)th
 * vertex.
 *
 * The texcoord format seems to be some signed 12.4 variant (perhaps 11.4)
 */

I (mesh_add_texcoords)
{
	vec2f_t texcoords[3];
	unsigned i;

	for (i = 0; i < 3; i++) {
		texcoords[i][0] = texcoord_to_float (inst[i+1] & 0xFFFF);
		texcoords[i][1] = texcoord_to_float (inst[i+1] >> 16);
	}

	VK_LOG ("GPU CP %08X: Tex Coord [%08X %08X %08X %08X] <%f %f> <%f %f> <%f %f>",
	        gpu->cp.pc,
	        inst[0], inst[1], inst[2], inst[3],
	        texcoords[0][0], texcoords[0][1],
	        texcoords[1][0], texcoords[1][1],
	        texcoords[2][0], texcoords[2][1]);

	hikaru_renderer_add_texcoords (gpu->renderer, texcoords);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
}

/* 158	Tex Coord Unknown
 *
 *	-------- u------- ----oooo oooooooo
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *
 * u = Unknown
 * v = Tex coords for the previous 12C instruction?
 */

I (mesh_add_texcoords_unk)
{
	uint16_t u = inst[1] & 0xFFFF;
	uint16_t v = inst[1] >> 16;

	VK_LOG ("GPU CP %08X: Tex Coord Unk [%08X %08X] { %u %u }",
	        gpu->cp.pc, inst[0], inst[1], u, v);

	gpu->cp.unhandled |= !!(inst[0] & 0xFF7FF000);
}

/* 154	Commit Alpha Threshold
 *
 *	-------- --nnnnnn ----oooo oooooooo
 *	hhhhhhhh hhhhhhhh hhhhhhhh llllllll
 *
 * n = Unknown
 * l = Alpha low threshold
 * h = Alpha high threshold
 *
 * See PH:@0C017798, PH:@0C0CF868. It may be related to
 * command C81, see PH:@0C0CF872 and PH:@0C0CF872.
 */

I (commit_alpha_threshold)
{
	unsigned n = (inst[0] >> 16) & 0x3F;
	int32_t thresh_lo = inst[1] & 0xFF;
	int32_t thresh_hi = signext_n_32 ((inst[1] >> 8), 23);

	VK_LOG ("GPU CP %08X: Commit Alpha Threshold [%08X %08X] %u <%X %X>",
	        gpu->cp.pc, inst[0], inst[1],
	        n, thresh_lo, thresh_hi);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFC0F000);
}

/* 194	Commit Ramp Data
 *
 *	nnnnnnnn mmmmmmmm ----oooo oooooooo
 *	aaaaaaaa aaaaaaaa bbbbbbbb bbbbbbbb
 *
 * NOTE: these come in groups of 8. The data for each group
 * comes from a different ptr.
 *
 * NOTE: seems to be light related.
 *
 * See PH:@0C017A3E.
 */

I (commit_ramp)
{
	uint32_t n = (inst[0] >> 24) & 0xFF;
	uint32_t m = (inst[0] >> 19) & 0x1F;
	uint32_t a = inst[1] & 0xFFFF;
	uint32_t b = inst[1] >> 16;

	VK_LOG ("GPU CP %08X: Commit Ramp Data [%08X %08X] %u %u <%X %X>",
	        gpu->cp.pc, inst[0], inst[1], n, m, a, b);

	gpu->cp.unhandled |= !!(inst[0] & 0x0000F000);
}

/* 3A1	Set Lo Addresses
 *
 *	-------- -------- ----oooo oooooooo
 *	llllllll llllllll llllllll llllllll
 *	LLLLLLLL LLLLLLLL LLLLLLLL LLLLLLLL
 *      -------- -------- -------- --------
 *
 * l,L = Low addresses?
 *
 * Comes in a pair with 5A1.
 *
 * See PH:@0C016308
 */

I (commit_addr_lo)
{
	VK_LOG ("GPU CP %08X: Set Lo Addresses [%08X %08X %08X %08X]",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
	gpu->cp.unhandled |= (inst[3] != 0);
}

/* 5A1	Set Hi Addresses
 *
 *	-------- -------- ----oooo oooooooo
 *	uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu
 *	UUUUUUUU UUUUUUUU UUUUUUUU UUUUUUUU
 *      -------- -------- -------- --------
 *
 * h,H = High addresses?
 *
 * Comes in a pair with 3A1.
 *
 * See PH:@0C016308
 */

I (commit_addr_hi)
{
	VK_LOG ("GPU CP %08X: Set Hi Addresses [%08X %08X %08X %08X]",
	        gpu->cp.pc, inst[0], inst[1], inst[2], inst[3]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFFF000);
	gpu->cp.unhandled |= (inst[3] != 0);
}

/* 6D1	Unknown
 *
 *	-------- ------nn ----oooo oooooooo
 *	bbbbbbbb bbbbbbbb cccccccc cccccccc
 *
 * These come in quartets. May be related to matrices. See PH:@0C015C3E. Note
 * that the values b and c here come from FPU computations, see PH:@0C0FF970.
 */

I (unk_6D1)
{
	unsigned a = inst[0] >> 16;
	unsigned b = inst[1] & 0xFFFF;
	unsigned c = inst[1] >> 16;

	VK_LOG ("GPU CP %08X: Unknown: Set 6D1 [%08X %08X] <%u %u %u>",
	        gpu->cp.pc, inst[0], inst[1], a, b, c);

	gpu->cp.unhandled |= !!(inst[0] & 0xFFFCF000);
}

/* 101	Set Unknown
 *
 *	----nnuu uuuuuuuu ----oooo oooooooo
 *
 * n, u = Unknown
 *
 * See @0C008040, PH:@0C016418, PH:@0C016446.
 */

I (unk_101)
{
	VK_LOG ("GPU CP %08X: Unknown: Set 101 [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xF000F000);
}

/* x01	Set Unknown
 *
 *	-------- unnnnnnn ----oooo oooooooo
 *
 * u, n = Unknown, n != 1
 */

I (unk_X01)
{
	VK_LOG ("GPU CP %08X: Unknown: Set %03X [%08X]", gpu->cp.pc, inst[0] & 0xFFF, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFF00F000);
}

/* 303	Recall Unknown
 * 903
 * D03
 * 313
 * D13
 *
 *	FFFFFFFF -------- ----NNNN oooooooo
 *
 * F = Fog-related value? See PH:@0C0DA8BC. (-1)
 * N = Unknown; 3 impies F is 0xFF; it can't be zero. (+1)
 *
 * NOTE: the actual value of N passed to the command uploading function is N-1.
 *
 * See PH:@0C0173CA. For evidence that these commands are related, see
 * PH:@0C0EEFBE.
 */

I (unk_X03)
{
	/* sub == 2 : disabled */
	/* sub == C : enabled */

	VK_LOG ("GPU CP %08X: Recall X03 [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0x00FFF000);
}

/* E88	Unknown
 *
 *	-------- U------- ----oooo oooooooo
 *
 * U = unknown
 *
 * Always comes as the last instruction. Perhaps some kind of 'flush all'
 * or 'raise IRQ' command or something like that. If it is a 'flush all'
 * command, it may set some GPU ports not set by 1C2 (1A000024 perhaps.)
 */

I (unk_E88)
{
	VK_LOG ("GPU CP %08X: Unknown E88 [%08X]", gpu->cp.pc, inst[0]);

	gpu->cp.unhandled |= !!(inst[0] & 0xFF7FF000);
}

I (invalid)
{
	gpu->cp.unhandled |= true;
}

#define D(op_, size_, name_) \
	{ op_, size_, hikaru_gpu_inst_##name_ }

#define D4(op_, name_, size_) \
	D (((op_) & ~0xF0F) | 0x10C, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10D, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10E, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10F, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0C, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0D, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0E, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0F, name_, size_)

#define D8(op_, name_, size_) \
	D (((op_) & ~0xF0F) | 0x108, name_, size_), \
	D (((op_) & ~0xF0F) | 0x109, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10A, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10B, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10C, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10D, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10E, name_, size_), \
	D (((op_) & ~0xF0F) | 0x10F, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF08, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF09, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0A, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0B, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0C, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0D, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0E, name_, size_), \
	D (((op_) & ~0xF0F) | 0xF0F, name_, size_)

static const struct {
	uint32_t op;
	uint32_t size;
	void (* handler)(hikaru_gpu_t *gpu, uint32_t *inst);
} cs_insns[] = {
	/* Control Flow */
	D (0x000, 4, nop),
	D (0x012, 0, jump),
	D (0x812, 0, jump),
	D (0x052, 0, call),
	D (0x852, 0, call),
	D (0x082, 0, ret),
	D (0x1C2, 4, kill),
	D (0x781, 4, sync),
	D (0x181, 4, unk_181),
	/* Viewports */
	D (0x021, 16, vp_set_projection),
	D (0x221, 16, vp_set_extents),
	D (0x421, 16, vp_set_depth),
	D (0x621, 16, vp_set_depth_queue),
	D (0x811, 8, vp_set_ambient_color),
	D (0x991, 8, vp_set_clear_color),
	D (0x004, 4, vp_commit),
	D (0x003, 4, vp_recall),
	/* Modelview Matrix */
	D (0x161, 16, modelview_set_vector),
	/* Materials */
	D (0x091, 8, mat_set_color_0_1),
	D (0x291, 8, mat_set_color_0_1),
	D (0x491, 8, mat_set_shininess),
	D (0x691, 8, mat_set_material_color),
	D (0x081, 4, mat_set_unk_081),
	D (0x881, 4, mat_set_flags),
	D (0xA81, 4, mat_set_blending),
	D (0xC81, 4, mat_set_unk_C81),
	D (0x084, 4, mat_commit),
	D (0x083, 4, mat_recall),
	/* Texheads */
	D (0x0C1, 4, tex_set_unk_0C1),
	D (0x2C1, 4, tex_set_format),
	D (0x4C1, 4, tex_set_slot),
	D (0x0C4, 4, tex_commit),
	D (0x0C3, 4, tex_recall),
	/* Lights */
	D (0x261, 16, lit_set_type),
	D (0x961, 16, lit_set_position),
	D (0xB61, 16, lit_set_direction),
	D (0x051, 8, lit_set_unk_051),
	D (0x451, 8, lit_set_unk_451),
	D (0x561, 16, lit_set_unk_561),
	D (0x006, 4, lit_unk_006),
	D (0x046, 4, lit_unk_046),
	D (0x104, 4, lit_commit),
	D (0x064, 16, lit_commit_set),
	D (0x043, 4, lit_recall_set),
	/* Mesh */
	D4 (0x1AC, 16, mesh_add_position),
	D4 (0x12C, 16, mesh_add_position_unk),
	D8 (0x1B8, 32, mesh_add_position_normal),
	D  (0xEE8, 16, mesh_add_texcoords),
	D  (0xEE9, 16, mesh_add_texcoords),
	D8 (0x158, 8, mesh_add_texcoords_unk),
	/* Unassigned */
	D (0x154, 8, commit_alpha_threshold),
	D (0x194, 8, commit_ramp),
	D (0x3A1, 16, commit_addr_lo),
	D (0x5A1, 16, commit_addr_hi),
	/* Unknown */
	D (0x6D1, 8, unk_6D1),
	D (0x101, 4, unk_101),
	D (0x301, 4, unk_X01),
	D (0x501, 4, unk_X01),
	D (0x901, 4, unk_X01),
	D (0x303, 4, unk_X03),
	D (0x903, 4, unk_X03),
	D (0xD03, 4, unk_X03),
	D (0x313, 4, unk_X03),
	D (0xD13, 4, unk_X03),
	D (0xE88, 4, unk_E88),
};

void
hikaru_gpu_cp_exec (hikaru_gpu_t *gpu, int cycles)
{
	if (!gpu->cp.is_running)
		return;

	gpu->materials.base = 0;
	gpu->texheads.base  = 0;
	gpu->lights.base    = 0;

	gpu->cp.cycles = cycles;
	while (gpu->cp.cycles > 0 && gpu->cp.is_running) {
		void (* handler)(hikaru_gpu_t *, uint32_t *);
		uint32_t inst[8] = { 0 }, op;

		gpu->cp.unhandled = false;

		if (fetch (gpu, inst)) {
			VK_ERROR ("GPU CP %08X: invalid PC, skipping CS", gpu->cp.pc);
			gpu->cp.is_running = false;
			break;
		}

		op = inst[0] & 0xFFF;
		if (!is_vertex_op (op))
			hikaru_renderer_end_mesh (gpu->renderer);

		handler = gpu->cp.insns[op].handler;
		VK_ASSERT (handler);
		handler (gpu, inst);

		if (gpu->cp.unhandled) {
			print_unhandled (gpu, inst, gpu->cp.insns[op].size / 4);
			/* We try to carry on anyway */
		}

		if (gpu->cp.insns[op].size)
			gpu->cp.pc += gpu->cp.insns[op].size;
		else
			gpu->cp.pc += 4;

		gpu->cp.cycles--;
	}

	if (!gpu->cp.is_running)
		hikaru_gpu_cp_end_processing (gpu);
}

void
hikaru_gpu_cp_init (hikaru_gpu_t *gpu)
{
	unsigned i;
	for (i = 0; i < 0x1000; i++) {
		gpu->cp.insns[i].handler = hikaru_gpu_inst_invalid;
		gpu->cp.insns[i].size = 0;
	}
	for (i = 0; i < NUMELEM (cs_insns); i++) {
		uint32_t op = cs_insns[i].op;
		gpu->cp.insns[op].handler = cs_insns[i].handler;
		gpu->cp.insns[op].size    = cs_insns[i].size;
	}
}
