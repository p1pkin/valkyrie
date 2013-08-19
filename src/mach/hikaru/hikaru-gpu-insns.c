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

#define PC        gpu->cp.pc
#define UNHANDLED gpu->cp.unhandled

static void
disasm (hikaru_gpu_t *gpu, uint32_t *inst, unsigned nwords, const char *fmt, ...)
{
	char out[256], *tmp = out;
	va_list args;
	unsigned i;

	VK_ASSERT (gpu);
	VK_ASSERT (inst);
	VK_ASSERT (nwords <= 8);

	if (!gpu->options.log_cp)
		return;

	va_start (args, fmt);
	tmp += sprintf (tmp, "CP @%08X : ", PC);
	for (i = 0; i < 8; i++) {
		if (i < nwords)
			tmp += sprintf (tmp, "%08X ", inst[i]);
		else
			tmp += sprintf (tmp, "........ ");
	}
	tmp += sprintf (tmp, "%c ", gpu->in_mesh ? 'M' : ' ');
	if (UNHANDLED)
		tmp += sprintf (tmp, " *UNHANDLED* ");
	vsnprintf (tmp, sizeof (out), fmt, args);
	va_end (args);

	VK_LOG ("%s", out);
}

#define DISASM(nwords_, fmt_, args_...) \
	disasm (gpu, inst, nwords_, fmt_, ##args_) \

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
	unsigned i = gpu->frame_type;
	VK_ASSERT ((gpu->cp.sp[i] >> 24) == 0x48);
	vk_buffer_put (gpu->cmdram, 4, gpu->cp.sp[i] & 0x3FFFFFF, PC);
	gpu->cp.sp[i] -= 4;
}

static void
pop_pc (hikaru_gpu_t *gpu)
{
	unsigned i = gpu->frame_type;
	gpu->cp.sp[i] += 4;
	VK_ASSERT ((gpu->cp.sp[i] >> 24) == 0x48);
	PC = vk_buffer_get (gpu->cmdram, 4, gpu->cp.sp[i] & 0x3FFFFFF) + 8;
}

#define I(name_) \
	static void hikaru_gpu_inst_##name_ (hikaru_gpu_t *gpu, uint32_t *inst)

/****************************************************************************
 Control Flow
****************************************************************************/

/* 000	Nop
 *
 *	-------- -------- -------o oooooooo
 */

I (0x000)
{
	UNHANDLED |= (inst[0] != 0);

	DISASM (1, "nop");
}

/* 012	Jump
 *
 *	-------- -------- ----R--o oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * R = Relative
 * A = Address or Offset in 32-bit words.
 */

I (0x012)
{
	uint32_t addr;

	addr = inst[1] * 4;
	if (inst[0] & 0x800)
		addr += PC;

	check_self_loop (gpu, addr);

	UNHANDLED |= !!(inst[0] & 0xFFFFF600);

	DISASM (2, "jump @%08X", addr);
	PC = addr;
}

/* 052	Call
 *
 *	-------- -------- ----R--o oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * R = Relative
 * A = Address or Offset in 32-bit words.
 */

I (0x052)
{
	uint32_t addr;

	addr = inst[1] * 4;
	if (inst[0] & 0x800)
		addr += PC;

	check_self_loop (gpu, addr);
	push_pc (gpu);

	UNHANDLED |= !!(inst[0] & 0xFFFFF600);

	DISASM (2, "call @%08X", addr);
	PC = addr;
}

/* 082	Return
 *
 *	-------- -------- -------o oooooooo
 */

I (0x082)
{
	pop_pc (gpu);

	UNHANDLED |= !!(inst[0] & 0xFFFFFE00);

	DISASM (1, "ret");
}

/* 1C2	Kill
 *
 *	-------- -------- -------o oooooooo
 */

I (0x1C2)
{
	gpu->cp.is_running = false;

	UNHANDLED |= !!(inst[0] & 0xFFFFFE00);

	DISASM (1, "kill");

}

/****************************************************************************
 Viewports
****************************************************************************/

/* 021	Viewport: Set Z Clip
 *
 *	-------- -------- -----00o oooooooo
 *	FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF
 *	ffffffff ffffffff ffffffff ffffffff
 *	nnnnnnnn nnnnnnnn nnnnnnnn nnnnnnnn
 *
 * F = far depth clipping plane
 * f = far depth clipping plane, again
 * n = near depth clipping plane
 *
 * Both f and F are computed as:
 *
 *  (height / 2) / tanf(some_angle / 2)
 *
 * which assuming some_angle is fovy, is the formula for computing the far
 * clipping planes. I have no idea why there are two identical entries tho.
 *
 * See PH:@0C01587C, PH:@0C0158A4, PH:@0C0158E8.
 *
 *
 * 221	Viewport: Set XY Clip
 *
 *	-------- -------- -----01o oooooooo
 *	jjjjjjjj jjjjjjjj cccccccc cccccccc
 *	--YYYYYY YYYYYYYY -XXXXXXX XXXXXXXX
 *	--yyyyyy yyyyyyyy -xxxxxxx xxxxxxxx
 *
 * c,j = center?
 * x,y = left, bottom clipping planes
 * X,Y = right, top clipping planes
 *
 * NOTE: according to the code, X can be at most 640, Y can be at most 512;
 * and at least one of x and y must be zero.
 *
 * See PH:@0C015924
 *
 *
 * 421	Viewport: Set Z Buffer Config
 *
 *	-------- -------- -----10o oooooooo
 *	nnnnnnnn nnnnnnnn nnnnnnnn nnnnnnnn
 *	ffffffff ffffffff ffffffff ffffffff
 *	FFF----- -------- -------- --------
 *
 * n = depth buffer minimum
 * f = depth buffer maximum
 * F = depth function
 *
 * See PH:@0C015AA6
 *
 *
 * 621	Viewport: Set Z Queue Config
 *
 *	-------- ----ttDu -----11o oooooooo
 *	AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
 *	dddddddd dddddddd dddddddd dddddddd 
 *	gggggggg gggggggg gggggggg gggggggg
 *
 * t = Type
 * D = Disable?
 * u = Unknown
 * RGBA = color/mask?
 *
 * f = queue density
 *
 *	Computed as 1.0f (constant), 1.0f / zdelta, or 1.0f / sqrt (zdelta**2);
 *	where zdelta = zmax - zmin.
 *
 * g = queue bias
 *
 *	Computed as depth_near / depth_far.
 *
 * See PH:@0C0159C4, PH:@0C015A02, PH:@0C015A3E.
 */

I (0x021)
{
	hikaru_gpu_viewport_t *vp = &gpu->viewports.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		vp->clip.f = *(float *) &inst[1];
		vp->clip.n = *(float *) &inst[3];

		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
		UNHANDLED |= inst[1] != inst[2];

		DISASM (4, "vp: set clip Z [f=%f n=%f]", vp->clip.f, vp->clip.n);
		break;

	case 2:
		vp->offset.x = (float) (inst[1] & 0xFFFF);
		vp->offset.y = (float) (inst[1] >> 16);

		vp->clip.l = (float) (inst[2] & 0x7FFF);
		vp->clip.r = (float) (inst[3] & 0x7FFF);
		vp->clip.b = (float) ((inst[2] >> 16) & 0x3FFF);
		vp->clip.t = (float) ((inst[3] >> 16) & 0x3FFF);
	
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
		UNHANDLED |= !!(inst[2] & 0xC0008000);
		UNHANDLED |= !!(inst[3] & 0xC0008000);
	
		DISASM (4, "vp: set clip XY [clipxy=(%f %f %f %f) offs=(%f,%f)]",
		        vp->clip.l, vp->clip.r, vp->clip.b, vp->clip.t,
		        vp->offset.x, vp->offset.y);
		break;

	case 4:
		vp->depth.min = *(float *) &inst[1];
		vp->depth.max = *(float *) &inst[2];
		vp->depth.func = inst[3] >> 29;

		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
		UNHANDLED |= !!(inst[3] & 0x1FFFFFFF);

		DISASM (4, "vp: set depth [func=%u range=(%f,%f)]",
		        vp->depth.func, vp->depth.min, vp->depth.max);
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

		UNHANDLED |= !!(inst[0] & 0xFFF0F800);
	
		DISASM (4, "vp: set depth queue [type=%u ena=%u unk=%u mask=(%X %X %X %X) density=%f bias=%f]",
			vp->depth.q_type, vp->depth.q_enabled, vp->depth.q_unknown,
			vp->depth.mask[0], vp->depth.mask[1],
			vp->depth.mask[2], vp->depth.mask[3],
			vp->depth.density, vp->depth.bias);
		break;
	}

	vp->flags |= HIKARU_GPU_OBJ_DIRTY;
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
	hikaru_gpu_viewport_t *vp = &gpu->viewports.scratch;

	vp->color.ambient[0] = inst[0] >> 16;
	vp->color.ambient[1] = inst[1] & 0xFFFF;
	vp->color.ambient[2] = inst[1] >> 16;

	UNHANDLED |= !!(inst[0] & 0x0000F600);
	UNHANDLED |= !(inst[0] & 0x00000800);

	DISASM (2, "vp: set ambient [%X %X %X]",
	        vp->color.ambient[2], vp->color.ambient[1], vp->color.ambient[0]);

	vp->flags |= HIKARU_GPU_OBJ_DIRTY;
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
	hikaru_gpu_viewport_t *vp = &gpu->viewports.scratch;

	vp->color.clear[0] = inst[1] & 0xFF;
	vp->color.clear[1] = (inst[1] >> 8) & 0xFF;
	vp->color.clear[2] = (inst[1] >> 16) & 0xFF;
	vp->color.clear[3] = ((inst[1] >> 24) & 1) ? 0xFF : 0;

	UNHANDLED |= !!(inst[0] & 0xFFFFF600);
	UNHANDLED |= !(inst[0] & 0x00000800);
	UNHANDLED |= !!(inst[0] & 0xFE000000);

	DISASM (2, "vp: set clear [%X %X %X %X]",
	        vp->color.clear[0], vp->color.clear[1],
	        vp->color.clear[2], vp->color.clear[3]);

	vp->flags |= HIKARU_GPU_OBJ_DIRTY;
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
	uint32_t index = (inst[0] >> 16) & 7;
	hikaru_gpu_viewport_t *vp = &gpu->viewports.table[index];

	*vp = gpu->viewports.scratch;

	UNHANDLED |= !!(inst[0] & 0xFFF8FE00);

	DISASM (1, "vp: commit @%u [%s]", index, get_gpu_viewport_str (vp));

	vp->flags = HIKARU_GPU_OBJ_SET | HIKARU_GPU_OBJ_DIRTY;
}

/* 003	Recall Viewport
 *
 *	-------- -----iii -UU----o oooooooo
 *
 * i = Index
 * U = Unknown (2003 and 4003 variants are used in BRAVEFF title screen)
 *
 * See PH:@0C015AF6, PH:@0C015B12, PH:@0C015B32.
 */

I (0x003)
{
	uint32_t index = (inst[0] >> 16) & 7;
	hikaru_gpu_viewport_t *vp = &gpu->viewports.scratch;

	*vp = gpu->viewports.table[index];
	if (!(vp->flags & HIKARU_GPU_OBJ_SET)) {
		VK_ERROR ("CP @%08X: recalled viewport was not set (%u), skipping", PC, index);
		return;
	}

	UNHANDLED |= !!(inst[0] & 0xFFF89E00);

	DISASM (1, "vp: recall @%u %c [%s]",
	        index, (gpu->viewports.table[index].flags & HIKARU_GPU_OBJ_SET) ? ' ' : '!',
	        get_gpu_viewport_str (vp));
}

/****************************************************************************
 Matrices
****************************************************************************/

/* 161	Set Matrix Vector
 *
 *	-------- ----UPnn ----000o oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * U = Unknown (Multiply? Mutually exclusive with P)
 * P = Push
 * n = Element index
 * x,y,z = Elements
 *
 * This command sets a column vector of the current modelview matrix. Typically
 * four of these commands follow and set the whole 4x3 modelview matrix.
 *
 * Clearly the fourth row is fixed to (0, 0, 0, 1).
 *
 * See @0C008080.
 *
 *
 * 561	Set Light Vector 1
 *
 *	-------- ------nn ----010o oooooooo
 *	-------- -------- -------- --------
 *	-------- -------- -------- --------
 *	-------- -------- -------- --------
 *
 * No idea.
 *
 *
 * 961	Set Light Vector 2
 *
 *	-------- -------e nnnn100o oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * e = Unknown
 * n = Unknown
 * x,y,z = position (XXX not necessarily)
 *
 * Variants include 16961, 10961, 8961. Apparently the 8961 variant makes use
 * of the 194 ramp data.
 *
 *
 * B61	Set Light Vector 3
 *
 *	-------- -------- Unnn110o oooooooo
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * U = Unknown
 * n = Unknown
 * x,y,z = direction (XXX not quite)
 */

I (0x161)
{
	hikaru_gpu_modelview_t *mv = &gpu->modelviews.stack[gpu->modelviews.depth];
	uint32_t push, elem;

	switch ((inst[0] >> 8) & 0xF) {

	case 1:
		push = (inst[0] >> 18) & 1;
		elem = (inst[0] >> 16) & 3;

		if (push && elem == 0) {
			/* XXX push the matrix stack */
		}

		/* We store the columns as rows to facilitate the
		 * translation to GL column-major convertion in the
		 * renderer. */
		mv->mtx[elem][0] = *(float *) &inst[1];
		mv->mtx[elem][1] = *(float *) &inst[2];
		mv->mtx[elem][2] = *(float *) &inst[3];
		mv->mtx[elem][3] = (elem == 3) ? 1.0f : 0.0f;

		DISASM (4, "mtx: set vector [%c %u (%f %f %f %f)]",
		        push ? 'P' : ' ', elem,
		        mv->mtx[elem][0], mv->mtx[elem][1],
		        mv->mtx[elem][2], mv->mtx[elem][3]);

		UNHANDLED |= !!(inst[0] & 0xFFF0F000);
		UNHANDLED |= !isfinite (mv->mtx[elem][0]);
		UNHANDLED |= !isfinite (mv->mtx[elem][1]);
		UNHANDLED |= !isfinite (mv->mtx[elem][2]);
		break;

	case 5:
		DISASM (4, "lit: set unknown");

		UNHANDLED |= !!(inst[0] & 0xFFFCF000);
		break;

	case 9:
		DISASM (4, "lit: set unknown [%f %f %f]",
		        *(float *) &inst[1], *(float *) &inst[2], *(float *) &inst[3]);

		UNHANDLED |= !!(inst[0] & 0xFFFE0000);
		break;

	case 0xB:
		DISASM (4, "lit: set unknown [%f %f %f]",
		        *(float *) &inst[1], *(float *) &inst[2], *(float *) &inst[3]);

		UNHANDLED |= !!(inst[0] & 0xFFFF0000);
		break;
	}
}

/****************************************************************************
 Materials
****************************************************************************/

/* 091	Material: Set Primary Color
 *
 *	-------- -------- -----00o oooooooo
 *	uuuuuuuu bbbbbbbb gggggggg rrrrrrrr
 *
 * u = Unknown, seen in BRAVEFF title screen.
 * r,g,b = RGB color
 *
 * See PH:@0C0CF742.
 *
 *
 * 291	Material: Set Secondary Color
 *
 *	-------- -------- -----01o oooooooo
 *	uuuuuuuu bbbbbbbb gggggggg rrrrrrrr
 *
 * r,g,b = RGB color
 *
 * See PH:@0C0CF742.
 *
 *
 * 491	Material: Set Shininess
 *
 *	-------- -------- -----10o oooooooo
 *	ssssssss bbbbbbbb gggggggg rrrrrrrr
 *
 * s = Specular shininess
 *
 * See PH:@0C0CF798, PH:@0C01782C.
 *
 *
 * 691	Material: Set Material Color
 *
 *	rrrrrrrr rrrrrrrr -----11o oooooooo
 *	bbbbbbbb bbbbbbbb gggggggg gggggggg
 *
 * See PH:@0C0CF7CC.
 *
 * NOTE: A91 and C91 are used in BRAVEFF title screen. They clearly alias A81
 * and C81.
 */

static void hikaru_gpu_inst_0x081 (hikaru_gpu_t *, uint32_t *);

I (0x091)
{
	hikaru_gpu_material_t *mat = &gpu->materials.scratch;
	uint32_t i;

	switch ((inst[0] >> 8) & 15) {
	case 0:
	case 2:
		i = (inst[0] >> 9) & 1;

		mat->color[i][0] = inst[1] & 0xFF;
		mat->color[i][1] = (inst[1] >> 8) & 0xFF;
		mat->color[i][2] = (inst[1] >> 16) & 0xFF;
	
		DISASM (2, "mat: set color %u [color=(%u %u %u)]", i,
		        mat->color[i][0], mat->color[i][1], mat->color[i][2]);
	
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
		break;

	case 4:
		mat->shininess[0] = inst[1] & 0xFF;
		mat->shininess[1] = (inst[1] >> 8) & 0xFF;
		mat->shininess[2] = (inst[1] >> 16) & 0xFF;
		mat->specularity = inst[1] >> 24;
	
		DISASM (2, "mat: set shininess [%u (%u %u %u)]",
		        mat->specularity, mat->shininess[2],
		        mat->shininess[1], mat->shininess[0]);
	
		UNHANDLED |= !!(inst[0] & 0xFFFFF800);
		break;

	case 6:
		mat->material_color[0] = inst[0] >> 16;
		mat->material_color[1] = inst[1] & 0xFFFF;
		mat->material_color[2] = inst[1] >> 16;
	
		DISASM (2, "mat: set material [(%u %u %u)]",
		        mat->material_color[0], mat->material_color[1],
		        mat->material_color[2]);
	
		UNHANDLED |= !!(inst[0] & 0x0000F800);
		break;

	case 0xA:
	case 0xC:
		hikaru_gpu_inst_0x081 (gpu, inst);
		/* XXX HACK */
		PC -= 4;
		break;
	}
}

/* 081	Material: Set Unknown
 *
 *	-------- ----mmmm ---n000o oooooooo
 *
 * n, m = Unknown
 *
 *
 * 881	Material: Set Flags
 *
 *	-------- --hatzSS ----ssso oooooooo
 *
 * S = Shading mode (flat, linear, phong)
 * z = Depth blend (fog)
 * t = Enable texture
 * a = Alpha mode
 * h = Highlight mode
 *
 * See PH:@0C0CF700.
 *
 *
 * A81	Material: Set Blending Mode
 *
 *	-------- ------mm ----ssso oooooooo
 *
 * m = Blending Mode
 *
 * See PH:@0C0CF7FA.
 *
 *
 * C81	Material: Set Unknown
 *
 *	-------- --U----- ----ssso oooooooo
 *
 * U = Unknown
 *
 * It may be related to command 154, see PH:@0C0CF872 and
 * PH:@0C0CF872.
 *
 * These can come in pairs, see e.g., AT:@0C0380AC.
 */

I (0x081)
{
	hikaru_gpu_material_t *mat = &gpu->materials.scratch;

	switch ((inst[0] >> 8) & 0xF) {
	case 0:
		DISASM (1, "mat: set unknown");

		UNHANDLED |= !!(inst[0] & 0xFFF0E000);
		break;

	case 8:
		mat->shading_mode	= (inst[0] >> 16) & 3;
		mat->depth_blend	= (inst[0] >> 18) & 1;
		mat->has_texture	= (inst[0] >> 19) & 1;
		mat->has_alpha		= (inst[0] >> 20) & 1;
		mat->has_highlight	= (inst[0] >> 21) & 1;

		DISASM (1, "mat: set flags [mode=%u zblend=%u tex=%u alpha=%u highl=%u",
			mat->shading_mode,
			mat->depth_blend,
			mat->has_texture,
			mat->has_alpha,
			mat->has_highlight);

		UNHANDLED |= !!(inst[0] & 0xFFC0F000);
		break;

	case 0xA:
		mat->blending_mode = (inst[0] >> 16) & 3;

		DISASM (1, "mat: set blending mode [mode=%u]", mat->blending_mode);

		UNHANDLED |= !!(inst[0] & 0xFFFCF000);
		break;

	case 0xC:
		DISASM (1, "mat: set unknown");

		UNHANDLED |= !!(inst[0] & 0xFFC0F000);
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
	uint32_t offset  = inst[0] >> 16;
	uint32_t index   = offset + gpu->materials.base;

	DISASM (1, "mat: commit @%u [offs=%u]", index, offset);

	if (index >= NUM_MATERIALS) {
		VK_ERROR ("CP: material commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_MATERIALS);
		return;
	}

	gpu->materials.table[index] = gpu->materials.scratch;
	gpu->materials.table[index].set = true;

	UNHANDLED |= !!(inst[0] & 0xFC00E000);
	UNHANDLED |= !(inst[0] & 0x1000);
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
	uint32_t offset = inst[0] >> 16;
	uint32_t make_active = (inst[0] >> 12) & 1;
	uint32_t index = gpu->materials.base + offset;

	if (make_active)
		DISASM (1, "mat: recall @%u [offset=%u] %c", index, offset,
		        gpu->materials.table[index].set ? ' ' : '!');
	else
		DISASM (1, "mat: set offset %u", offset);

	if (!make_active)
		gpu->materials.base = offset;
	else {
		if (index >= NUM_MATERIALS) {
			VK_ERROR ("CP: material recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_MATERIALS);
			UNHANDLED |= true;
			return;
		}
		if (!gpu->materials.table[index].set) {
			VK_ERROR ("CP: recalled material was not set (%u), skipping",
			          index);
			return;
		}

		gpu->materials.scratch = gpu->materials.table[index];
	}

	UNHANDLED |= !!(inst[0] & 0x0000E000);
}

/****************************************************************************
 Texheads
****************************************************************************/

/* 0C1	Texhead: Set Bias
 *
 *	----BBBB BBBB--xE -----00o oooooooo
 *
 * B = Unknown parameter, some form of texture bias? e.g., for mipmapping.
 * x = Used in BRAVEFF title screen.
 * E = Enabled.
 *
 * See PH:@0C015B7A. It may be related to lighting/highlight/emission. See
 * usage in BRAVEFF title screen.
 *
 *
 * 2C1	Texhead: Set Format/Size
 *
 *	888FFFll llHHHWWW uu---01o oooooooo
 *
 * 8 = Unknown		[argument on stack]
 * F = Format		[argument R7]
 * H = log16 of Height	[argument R6]
 * l = Unknown		[lower four bits of argument R4]
 * W = log16 of Width	[argument R5]
 * u = Unknown		[upper two bits of argument R4]
 *
 * See PH:@0C015BCC.
 *
 *
 * 4C1	Texhead: Set Slot
 *
 *	nnnnnnnn mmmmmmmm ---b-10o oooooooo
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
	hikaru_gpu_texhead_t *th = &gpu->texheads.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		th->_0C1_nibble	= (inst[0] >> 16) & 1;
		th->_0C1_byte	= (inst[0] >> 20) & 0xFF;

		DISASM (1, "tex: set bias [ena=%u %X]",
		        th->_0C1_nibble, th->_0C1_byte);

		UNHANDLED |= !!(inst[0] & 0xF00CF800);
		break;

	case 2:
		th->width	= 16 << ((inst[0] >> 16) & 7);
		th->height	= 16 << ((inst[0] >> 19) & 7);
		th->format	= (inst[0] >> 26) & 7;
		th->_2C1_unk4	= (((inst[0] >> 14) & 3) << 4) |
	                           ((inst[0] >> 22) & 15);
		th->_2C1_unk8	= inst[0] >> 29;

		/* XXX move this to the renderer */
		if (th->format == HIKARU_FORMAT_ABGR1111)
			th->width *= 2; /* pixels per word */

		DISASM (1, "tex: set format [%ux%u fmt=%u]",
		        th->width, th->height, th->format);

		UNHANDLED |= !!(inst[0] & 0x00003800);
		break;

	case 4:
		th->bank  = (inst[0] >> 12) & 1;
		th->slotx = (inst[0] >> 16) & 0xFF;
		th->sloty = inst[0] >> 24;

		DISASM (1, "tex: set slot [%u (%X,%X)]",
		        th->bank, th->slotx, th->sloty);

		UNHANDLED |= !!(inst[0] & 0x0000E000);
		break;
	}
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

I (0x0C4)
{
	uint32_t offset  = inst[0] >> 16;
	uint32_t index   = offset + gpu->texheads.base;

	DISASM (1, "tex: commit @%u [offset=%u]", index, offset);

	if (index >= NUM_TEXHEADS) {
		VK_ERROR ("CP: texhead commit index exceedes MAX (%u >= %u), skipping",
		          index, NUM_MATERIALS);
		return;
	}

	gpu->texheads.table[index] = gpu->texheads.scratch;
	gpu->texheads.table[index].set = true;

	UNHANDLED |= !!(inst[0] & 0xFC00E000);
	UNHANDLED |= ((inst[0] & 0x1000) != 0x1000);
}

/* 0C3	Recall Texhead
 *
 *	nnnnnnnn nnnnnnnn ---M---o oooooooo
 *
 * n = Index
 * M = Modifier: 0 = set base only, 1 = recall for real
 *
 * XXX n here is likely too large.
 */

I (0x0C3)
{
	uint32_t offset = inst[0] >> 16;
	uint32_t make_active = (inst[0] >> 12) & 1;
	uint32_t index = gpu->texheads.base + offset;

	if (make_active)
		DISASM (1, "tex: recall @%u [offset=%u] %c", index, offset,
		        gpu->texheads.table[index].set ? ' ' : '!');
	else
		DISASM (1, "tex: set offset [offset=%u]", offset);

	if (!make_active)
		gpu->texheads.base = offset;
	else {
		if (index >= NUM_TEXHEADS) {
			VK_ERROR ("CP: texhead recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_TEXHEADS);
			UNHANDLED |= true;
			return;
		}
		if (!gpu->texheads.table[index].set) {
			VK_ERROR ("CP: recalled texhead was not set (%u), skipping",
			          index);
			return;
		}

		gpu->texheads.scratch = gpu->texheads.table[index];
	}

	UNHANDLED |= !!(inst[0] & 0x0000E000);
}

/****************************************************************************
 Lights
****************************************************************************/

/* 061	Set Light Type/Unknown
 *
 *	-------- ------tt ----oooo oooooooo
 *	pppppppp pppppppp pppppppp pppppppp
 *	qqqqqqqq qqqqqqqq qqqqqqqq qqqqqqqq
 *	???????? ???????? ???????? ????????
 *
 * t = Light type
 * p = Unknown \ power/emission/exponent/dacay/XXX
 * q = Unknown /
 * ? = Used in BRAVEFF title?
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

I (0x061)
{
	hikaru_gpu_light_t *lit = &gpu->lights.scratch;

	lit->emission_type	= (inst[0] >> 16) & 3;
	lit->emission_p		= *(float *) &inst[1];
	lit->emission_q		= *(float *) &inst[2];

	DISASM (4, "lit: set type [type=%u p=%f q=%f]",
	        lit->emission_type, lit->emission_p, lit->emission_q);

	UNHANDLED |= !!(inst[0] & 0xFFFCF000);
	UNHANDLED |= !isfinite (lit->emission_p);
	UNHANDLED |= !isfinite (lit->emission_q);
}

/* 051	Light: Set Color-like
 *
 *	-------- nnnnnnnn -----0-o oooooooo
 *	--aaaaaa aaaabbbb bbbbbbcc cccccccc
 *
 * n = Index? Index into the 194 ramp data?
 * a,b,c = Color? = FP * 255.0f and then truncated and clamped to [0,FF].
 *
 * This may well be a 10-10-10 color format.
 *
 * See PH:@0C0178C6; for a,b,c computation see PH:@0C03DC66.
 *
 *
 * 451	Light: Set Color-like 2
 *
 *	-------u -------- -----1-o oooooooo
 *	???????? ???????? ???????? ????????
 *
 * u = Unknown
 * ? = Color-like if B61 was called on this light, garbage otherwise.
 *
 * See PH:@0C017A7C, PH:@0C017B6C, PH:@0C017C58, PH:@0C017CD4, PH:@0C017D64.
 */

I (0x051)
{
	hikaru_gpu_light_t *lit = &gpu->lights.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		lit->_051_index    = (inst[0] >> 16) & 0xFF;
		lit->_051_color[0] = inst[1] & 0x3FF;
		lit->_051_color[1] = (inst[1] >> 10) & 0x3FF;
		lit->_051_color[2] = (inst[1] >> 20) & 0x3FF;

		DISASM (2, "lit: set color-like [%u (%u %u %u)]",
		        lit->_051_index, lit->_051_color[2],
		        lit->_051_color[1], lit->_051_color[0]);

		UNHANDLED |= !!(inst[0] & 0xFF00F000);
		UNHANDLED |= !!(inst[1] & 0xC0000000);
		break;

	case 4:
		DISASM (2, "lit: set unknown");

		UNHANDLED |= !!(inst[0] & 0xFEFFF000);
		break;
	}
}

/* 006	Light: Unknown
 *
 *	-------- -------- -------o oooooooo
 */

I (0x006)
{
	DISASM (1, "lit: unknown");

	UNHANDLED |= !!(inst[0] & 0xFFFFF000);
}

/* 046	Light: Unknown
 *
 *	-------- -------n ----oooo oooooooo
 *
 * n = Unknown
 */

I (0x046)
{
	DISASM (1, "lit: unknown");

	UNHANDLED |= !!(inst[0] & 0xFFFEF000);
}

/* 104	Commit Light
 *
 *	------nn nnnnnnnn ----oooo oooooooo
 *
 * n = Index
 */

I (0x104)
{
	unsigned index = inst[0] >> 16;

	DISASM (1, "lit: commit @%u", index);

	if (index >= NUM_LIGHTS) {
		VK_ERROR ("CP: light commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_LIGHTS);
		return;
	}

	gpu->lights.table[index] = gpu->lights.scratch;
	gpu->lights.table[index].set = true;

	UNHANDLED |= !!(inst[0] & 0xFC00F000);
}

/* 064  Commit Lightset
 *
 *      -------- nnnnnnnn ---M---o oooooooo
 *      ------bb bbbbbbbb ------aa aaaaaaaa
 *      ------dd dddddddd ------cc cccccccc
 *      ???????? ???????? ???????? ????????
 *
 * M = Unknown (0 in the BOOTROM, 1 elsewhere)
 * n = Lightset index
 * a,b,c,d = Indices of four lights
 * ? = Used in BRAVEFF title?
 *
 * See PH:@0C017DF0.
 */

I (0x064)
{
	uint32_t offset = (inst[0] >> 16) & 0xFF;
	uint32_t light0 = inst[1] & 0x3FF;
	uint32_t light1 = (inst[1] >> 16) & 0x3FF;
	uint32_t light2 = inst[2] & 0x3FF;
	uint32_t light3 = (inst[2] >> 16) & 0x3FF;
	uint32_t index = offset + gpu->lights.base;

	DISASM (4, "lit: commit set @%u [base=%u]", index, gpu->lights.base);

	if (index >= NUM_LIGHTSETS) {
		VK_ERROR ("CP: lightset commit index exceeds MAX (%u >= %u), skipping",
		          index, NUM_LIGHTSETS);
		return;
	}
	if (!gpu->lights.table[light0].set ||
	    !gpu->lights.table[light1].set ||
	    !gpu->lights.table[light2].set ||
	    !gpu->lights.table[light3].set) {
		VK_ERROR ("CP: lightset commit includes unset lights (%u,%u,%u,%u), skipping",
		          light0, light1, light2, light3);
		return;
	}

	gpu->lights.sets[index].lights[0] = &gpu->lights.table[light0];
	gpu->lights.sets[index].lights[1] = &gpu->lights.table[light1];
	gpu->lights.sets[index].lights[2] = &gpu->lights.table[light2];
	gpu->lights.sets[index].lights[3] = &gpu->lights.table[light3];
	gpu->lights.sets[index].set = true;

	UNHANDLED |= !!(inst[0] & 0xFF00E000);
	UNHANDLED |= ((inst[0] & 0x1000) != 0x1000);
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
	uint32_t make_active = (inst[0] >> 12) & 1;
	uint32_t offset = (inst[0] >> 16) & 0xFF;
	uint32_t enabled_mask = (inst[0] >> 24) & 0xF;
	uint32_t index = gpu->lights.base + offset;

	DISASM (1, "lit: recall @%u", index);

	if (!make_active)
		gpu->lights.base = offset;
	else {
		if (index >= NUM_LIGHTSETS) {
			VK_ERROR ("CP: lightset recall index exceeds MAX (%u >= %u), skipping",
			          index, NUM_LIGHTSETS);
			UNHANDLED |= true;
			return;
		}
		if (!gpu->lights.sets[index].set) {
			VK_ERROR ("CP: recalled lightset was not set (%u), skipping",
			          index);
			UNHANDLED |= true;
			return;
		}
	}

	UNHANDLED |= !!(inst[0] & 0xF000E000);
}

/****************************************************************************
 Meshes
****************************************************************************/

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
		DISASM (1, "mesh: set unknown [%u]", (inst[0] >> 16) & 0x3FF);
	
		UNHANDLED |= !!(inst[0] & 0xF000F000);
		break;

	case 3:
		DISASM (1, "mesh: set unknown [%u]", (inst[0] >> 16) & 0xFF);
	
		UNHANDLED |= !!(inst[0] & 0xFF00F000);
		break;

	case 5:
		DISASM (1, "mesh: set unknown [%u]", (inst[0] >> 16) & 0x1F);
	
		UNHANDLED |= !!(inst[0] & 0xFFE0F000);
		break;

	case 9:
		log = (inst[0] >> 16) & 0xFF;
		gpu->static_mesh_precision = 1.0f / (1 << (0x8F - log - 2));

		DISASM (1, "mesh: set precision s [%u %f]", log, gpu->static_mesh_precision);
	
		UNHANDLED |= !!(inst[0] & 0xFF00F000);
		break;
	}
}

/* 12x	Mesh: Push Position (Static)
 * 1Ax	Mesh: Push Position (Dynamic)
 * 1Bx	Mesh: Push All (Position, Normal, Texcoords) (Dynamic)
 *
 *
 * They appear to have a common 32-bit header:
 *
 *	AAAAAAAA U------- uuuSTTTo oooootpW
 *
 * A = Vertex alpha
 *
 * U = Unknown
 *
 *     Normal smoothing?
 *
 * u = Unknown
 *
 *     No idea.
 *
 * S = Unknown
 *
 *     Seemingly used for shadows in AIRTRIX (attract mode).
 *
 * T = Triangle
 *
 *     The only observed values so far are 0 and 7.
 *
 *     If 0, the vertex is pushed to the GPU vertex buffer. If 7, the vertex is
 *     pushed and defines a triangle along with the three previously pushed
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
 *	xxxxxxxx xxxxxxxx ???????? ????????
 *	yyyyyyyy yyyyyyyy ???????? ????????
 *	zzzzzzzz zzzzzzzz ???????? ????????
 *
 * x,y,z = Vertex position.
 * ? = Normal in fixed-point? The sum of the three fields over distinct
 *     vectors in a mesh isn't constant tho.
 *
 * For 1BX, the rest of the instruction looks like:
 *
 *	-------- -------- -------- --------
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * x,y,z = Vertex position
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

static void
decode_vertex_header (hikaru_gpu_vertex_t *v, uint32_t inst0)
{
	memset ((void *) v, 0, sizeof (hikaru_gpu_vertex_t));

	v->info.full = inst0;

	VK_ASSERT (v->info.bit.tricap == 0 || v->info.bit.tricap == 7);
}

static float
texcoord_to_float (uint32_t x)
{
	int32_t u = (x & 0x8000) ? (x | 0xFFFF8000) : x;
	return u / 16.0f;
}

I (0x12C)
{
	hikaru_gpu_vertex_t v;

	decode_vertex_header (&v, inst[0]);

	v.pos[0] = (int16_t)(inst[1] >> 16) * gpu->static_mesh_precision;
	v.pos[1] = (int16_t)(inst[2] >> 16) * gpu->static_mesh_precision;
	v.pos[2] = (int16_t)(inst[3] >> 16) * gpu->static_mesh_precision;

	hikaru_renderer_push_vertices ((hikaru_renderer_t *) gpu->renderer,
	                               &v, HR_PUSH_POS, 1);

	UNHANDLED |= !!(inst[0] & 0x007F0000);

	DISASM (4, "mesh: push pos s [%s]", get_gpu_vertex_str (&v));
}

I (0x1AC)
{
	hikaru_gpu_vertex_t v;

	decode_vertex_header (&v, inst[0]);

	v.pos[0] = *(float *) &inst[1];
	v.pos[1] = *(float *) &inst[2];
	v.pos[2] = *(float *) &inst[3];

	hikaru_renderer_push_vertices ((hikaru_renderer_t *) gpu->renderer,
	                               &v, HR_PUSH_POS, 1);

	UNHANDLED |= !!(inst[0] & 0x007F0000);

	DISASM (4, "mesh: push pos d [%s]", get_gpu_vertex_str (&v));
}

I (0x1B8)
{
	hikaru_gpu_vertex_t v;

	decode_vertex_header (&v, inst[0]);

	v.pos[0] = *(float *) &inst[1];
	v.pos[1] = *(float *) &inst[2];
	v.pos[2] = *(float *) &inst[3];

	v.nrm[0] = *(float *) &inst[5];
	v.nrm[1] = *(float *) &inst[6];
	v.nrm[2] = *(float *) &inst[7];

	v.txc[0] = texcoord_to_float (inst[4] & 0xFFFF);
	v.txc[1] = texcoord_to_float (inst[4] >> 16);

	hikaru_renderer_push_vertices ((hikaru_renderer_t *) gpu->renderer, &v,
	                               HR_PUSH_POS | HR_PUSH_NRM | HR_PUSH_TXC, 1);

	UNHANDLED |= !!(inst[0] & 0x007F0000);

	DISASM (8, "mesh: push all d [%s]", get_gpu_vertex_str (&v));
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
	hikaru_gpu_vertex_t vs[3];
	unsigned i;

	for (i = 0; i < 3; i++) {
		decode_vertex_header (&vs[i], inst[0]);

		vs[i].txc[0] = ((int16_t) inst[i+1]) / 16.0f;
		vs[i].txc[1] = ((int16_t) (inst[i+1] >> 16)) / 16.0f;
	}

	hikaru_renderer_push_vertices (gpu->renderer, &vs[0], HR_PUSH_TXC, 3);

	UNHANDLED |= !!(inst[0] & 0xFFFEF000);

	DISASM (4, "mesh: push txc 3");
	DISASM (4, "      .......... 0: %s", get_gpu_vertex_str (&vs[0]));
	DISASM (4, "      .......... 1: %s", get_gpu_vertex_str (&vs[1]));
	DISASM (4, "      .......... 2: %s", get_gpu_vertex_str (&vs[2]));
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
	hikaru_gpu_vertex_t v;

	decode_vertex_header (&v, inst[0]);

	v.txc[0] = ((int16_t) inst[1]) / 16.0f;
	v.txc[1] = ((int16_t) (inst[1] >> 16)) / 16.0f;

	hikaru_renderer_push_vertices (gpu->renderer, &v, HR_PUSH_TXC, 1);

	UNHANDLED |= !!(inst[0] & 0xFF7FF000);

	DISASM (2, "mesh: push txc 1 [%s]", get_gpu_vertex_str (&v));
}

/****************************************************************************
 Unknown
****************************************************************************/

/* 181	Unknown: Sync
 *
 *	-------E nnnnnnnn -----00o oooooooo [181]
 *
 * E = Enable
 * n = Unknown
 *
 * E is set only if n is non-zero.
 *
 * See PH:@0C015B50. Probably related to 781, see PH:@0C038952.
 *
 *
 *	-----p-q -----P-Q -----11o oooooooo [781]
 *
 * p, q, P, Q = Unknown
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
 * CaH4e3 suggests the two are related to screen transitions. He's probably
 * right. :-)
 *
 * See @0C0065D6, PH:@0C016336, PH:@0C038952, PH:@0C015B50.
 */

I (0x181)
{
	switch ((inst[0] >> 8) & 7) {
	case 1:
		DISASM (1, "unk: sync 1");

		UNHANDLED |= !!(inst[0] & 0xFE00F800);
		break;
	case 7:
		DISASM (1, "unk: sync 7");

		UNHANDLED |= !!(inst[0] & 0xFAFAF800);
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
	DISASM (1, "unk: unknown");

	UNHANDLED |= !!(inst[0] & 0xFF7FF000);
}


/* 154	Commit Alpha Threshold
 *
 *	-------- --nnnnnn -------o oooooooo
 *	hhhhhhhh hhhhhhhh hhhhhhhh llllllll
 *
 * n = Unknown
 * l = Alpha low threshold
 * h = Alpha high threshold
 *
 * See PH:@0C017798, PH:@0C0CF868. It may be related to
 * command C81, see PH:@0C0CF872 and PH:@0C0CF872.
 */

I (0x154)
{
	unsigned n = (inst[0] >> 16) & 0x3F;
	int32_t thresh_lo = inst[1] & 0xFF;
	int32_t thresh_hi = signext_n_32 ((inst[1] >> 8), 23);

	DISASM (2, "unk: set alpha thresh [%u (%f %f)]",
	        n, thresh_lo, thresh_hi);

	UNHANDLED |= !!(inst[0] & 0xFFC0F000);
}

/* 194	Commit Ramp Data
 *
 *	nnnnnnnn mmmmmmmm -------o oooooooo
 *	aaaaaaaa aaaaaaaa bbbbbbbb bbbbbbbb
 *
 * NOTE: these come in groups of 8. The data for each group
 * comes from a different ptr.
 *
 * NOTE: seems to be light related.
 *
 * See PH:@0C017A3E.
 */

I (0x194)
{
	uint32_t n = (inst[0] >> 24) & 0xFF;
	uint32_t m = (inst[0] >> 19) & 0x1F;
	uint32_t a = inst[1] & 0xFFFF;
	uint32_t b = inst[1] >> 16;

	DISASM (2, "unk: set ramp [%u %u (%f %f)]", n, m, a, b);

	UNHANDLED |= !!(inst[0] & 0x0000F000);
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
	DISASM (4, "unk: set address");

	UNHANDLED |= !!(inst[0] & 0xFFFFF000);
	UNHANDLED |= (inst[3] != 0);
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
	unsigned a = inst[0] >> 16;
	unsigned b = inst[1] & 0xFFFF;
	unsigned c = inst[1] >> 16;

	DISASM (2, "unk: unknown [(%u %u %u)]", a, b, c);

	UNHANDLED |= !!(inst[0] & 0xFFFCF000);
}

/* 103	Recall Unknown
 * 113	Recall Unknown
 *
 *	FFFFFFFF -------- ----ssso oooooooo
 *
 * s = Sub-opcode
 * F = Fog-related value? See PH:@0C0DA8BC. (-1)
 *
 * Notes: The sub-opcode
 *
 *  3: disabled (it always comes with F=0 or F=0xFF).
 *  9: enabled, F is positive.
 *  D: enabled, F is negative (actual value is ~F).
 *
 * See AT:@0C049CDA for N=8 and N=C, see PH:@0C0173CA for N=2, N=8. The
 * commands are emitted at e.g. AT:@0C69A220 (all three of them).
 *
 * BRAVEFF title screen requires that it also resets the modelview matrix (only
 * the translation component is uploaded after calling 103).
 */

static void
reset_modelview (hikaru_gpu_t *gpu)
{
	hikaru_gpu_modelview_t *mv = &gpu->modelviews.stack[gpu->modelviews.depth];
	unsigned i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			mv->mtx[i][j] = (i == j) ? 1.0f : 0.0;
}

I (0x103)
{
	bool enabled;
	float kappa;

	UNHANDLED |= !!(inst[0] & 0x00FFF000);

	switch ((inst[0] >> 8) & 15) {
	case 3:
		enabled = false;
		DISASM (1, "vp: disable unk");
		break;
	case 9:
		enabled = true;
		kappa = ((int32_t)(uint8_t)(inst[0] >> 24)) / 255.0f;
		reset_modelview (gpu);
		DISASM (1, "vp: set identity; enable unk [kappa=%f]", kappa);
		break;
	case 0xD:
		enabled = true;
		kappa = ((int32_t)(int8_t)(~(inst[0] >> 24))) / 255.0f;
		reset_modelview (gpu);
		DISASM (1, "vp: set identity; enable unk [kappa=%f]", kappa);
		break;
	default:
		VK_ASSERT (0);
		break;
	}
}

/****************************************************************************
 Opcodes
****************************************************************************/

static struct {
	void (* handler)(hikaru_gpu_t *, uint32_t *);
	uint16_t size, flags;
} insns[0x200];

#define FLAG_JUMP	(1 << 0)
#define FLAG_BEGIN	(1 << 1)
#define FLAG_CONTINUE	(1 << 2)
#define FLAG_PUSH	(FLAG_BEGIN | FLAG_CONTINUE)
#define FLAG_STATIC	(1 << 3)
#define FLAG_INVALID	(1 << 4)

#define D(op_, base_op_, size_, flags_) \
	{ op_, size_, flags_, hikaru_gpu_inst_##base_op_ }

static const struct {
	uint32_t op;
	uint16_t size;
	uint16_t flags;
	void (* handler)(hikaru_gpu_t *gpu, uint32_t *inst);
} insns_desc[] = {
	/* 0x00 */
	D(0x000, 0x000, 4,	FLAG_CONTINUE),
	D(0x003, 0x003, 4,	0),
	D(0x004, 0x004, 4,	0),
	D(0x006, 0x006, 4,	0),
	D(0x011, 0x011, 8,	0),
	D(0x012, 0x012, 8,	FLAG_JUMP),
	D(0x021, 0x021, 16,	0),
	/* 0x40 */
	D(0x043, 0x043, 4,	0),
	D(0x046, 0x046, 4,	0),
	D(0x051, 0x051, 8,	0),
	D(0x052, 0x052, 8,	FLAG_JUMP),
	D(0x061, 0x061, 16,	0),
	D(0x064, 0x064, 16,	0),
	/* 0x80 */
	D(0x081, 0x081, 4,	FLAG_CONTINUE),
	D(0x082, 0x082, 4,	FLAG_JUMP),
	D(0x083, 0x083, 4,	FLAG_CONTINUE),
	D(0x084, 0x084, 4,	0),
	D(0x088, 0x088, 4,	0),
	D(0x091, 0x091, 8,	FLAG_CONTINUE),
	/* 0xC0 */
	D(0x0C1, 0x0C1, 4,	0),
	D(0x0C3, 0x0C3, 4,	0),
	D(0x0C4, 0x0C4, 4,	0),
	D(0x0D1, 0x0D1, 8,	0),
	D(0x0E8, 0x0E8, 16,	FLAG_PUSH),
	D(0x0E9, 0x0E8, 16,	FLAG_PUSH),
	/* 0x100 */
	D(0x101, 0x101, 4,	0),
	D(0x103, 0x103, 4,	0),
	D(0x104, 0x104, 4,	0),
	D(0x113, 0x103, 4,	0),
	D(0x12C, 0x12C, 16,	FLAG_PUSH | FLAG_STATIC),
	D(0x12D, 0x12C, 16,	FLAG_PUSH | FLAG_STATIC),
	D(0x12E, 0x12C, 16,	FLAG_PUSH | FLAG_STATIC),
	D(0x12F, 0x12C, 16,	FLAG_PUSH | FLAG_STATIC),
	/* 0x140 */
	D(0x154, 0x154, 8,	0),
	D(0x158, 0x158, 8,	FLAG_PUSH),
	D(0x159, 0x158, 8,	FLAG_PUSH),
	D(0x15A, 0x158, 8,	FLAG_PUSH),
	D(0x15B, 0x158, 8,	FLAG_PUSH),
	D(0x161, 0x161, 16,	0),
	/* 0x180 */
	D(0x181, 0x181, 4,	0),
	D(0x191, 0x191, 8,	0),
	D(0x194, 0x194, 8,	0),
	D(0x1A1, 0x1A1, 16,	0),
	D(0x1AC, 0x1AC, 16,	FLAG_PUSH),
	D(0x1AD, 0x1AC, 16,	FLAG_PUSH),
	D(0x1AE, 0x1AC, 16,	FLAG_PUSH),
	D(0x1AF, 0x1AC, 16,	FLAG_PUSH),
	D(0x1B8, 0x1B8, 32,	FLAG_PUSH),
	D(0x1B9, 0x1B8, 32,	FLAG_PUSH),
	D(0x1BA, 0x1B8, 32,	FLAG_PUSH),
	D(0x1BB, 0x1B8, 32,	FLAG_PUSH),
	D(0x1BC, 0x1B8, 32,	FLAG_PUSH),
	D(0x1BD, 0x1B8, 32,	FLAG_PUSH),
	D(0x1BE, 0x1B8, 32,	FLAG_PUSH),
	D(0x1BF, 0x1B8, 32,	FLAG_PUSH),
	/* 0x1C0 */
	D(0x1C2, 0x1C2, 4,	FLAG_JUMP)
};

void
hikaru_gpu_cp_init (hikaru_gpu_t *gpu)
{
	unsigned i;
	for (i = 0; i < 0x200; i++) {
		insns[i].handler = NULL;
		insns[i].size = 0;
		insns[i].flags = FLAG_INVALID;
	}
	for (i = 0; i < NUMELEM (insns_desc); i++) {
		uint32_t op = insns_desc[i].op;
		insns[op].handler = insns_desc[i].handler;
		insns[op].size    = insns_desc[i].size;
		insns[op].flags   = insns_desc[i].flags;
	}
}

/****************************************************************************
 Execution
****************************************************************************/

static int
fetch (hikaru_gpu_t *gpu, uint32_t **inst)
{
	hikaru_t *hikaru = (hikaru_t *) gpu->base.mach;

	/* The CS program has been observed to lie only in CMDRAM and slave
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

static uint32_t breakpoint = 0xFFFFFFFF;

void
hikaru_gpu_cp_exec (hikaru_gpu_t *gpu, int cycles)
{
	if (!gpu->cp.is_running)
		return;

	/* XXX shouldn't be setting in_mesh here; we may be building a mesh
	 * from the previous cycle batch. */
	gpu->in_mesh = false;

	gpu->materials.base = 0;
	gpu->texheads.base  = 0;
	gpu->lights.base    = 0;

	while (cycles > 0 && gpu->cp.is_running) {
		uint32_t *inst, op;
		uint16_t flags;

		if (PC == breakpoint)
			break;

		if (fetch (gpu, &inst)) {
			VK_ERROR ("CP %08X: invalid PC, skipping CS", PC);
			gpu->cp.is_running = false;
			break;
		}

		op = inst[0] & 0x1FF;

		flags = insns[op].flags;
		VK_ASSERT (!(flags & FLAG_INVALID));

		if (!gpu->in_mesh && (flags & FLAG_BEGIN)) {
			bool is_static = (flags & FLAG_STATIC) != 0;
			hikaru_renderer_begin_mesh (gpu->renderer, PC, is_static);
			gpu->in_mesh = true;
		} else if (gpu->in_mesh && !(flags & FLAG_CONTINUE)) {
			hikaru_renderer_end_mesh (gpu->renderer, PC);
			gpu->in_mesh = false;
		}

		gpu->cp.unhandled = false;
		insns[op].handler (gpu, inst);
		if (gpu->cp.unhandled) {
			VK_LOG ("CP @%08X: unhandled instruction [%08X]", PC, *inst)
			/* We carry on anyway */
		}

		if (!(flags & FLAG_JUMP))
			PC += insns[op].size;

	skip:
		cycles--;
	}

	if (!gpu->cp.is_running)
		hikaru_gpu_cp_end_processing (gpu);
}
