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

#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-gpu-private.h"
#include "mach/hikaru/hikaru-renderer.h"

static bool
is_valid_addr (uint32_t addr)
{
	return (addr >= 0x40000000 && addr <= 0x41FFFFFF) ||
	       (addr >= 0x48000000 && addr <= 0x483FFFFF) ||
	       (addr >= 0x4C000000 && addr <= 0x4C3FFFFF);
}

static void
push_pc (hikaru_gpu_t *gpu)
{
	unsigned i = gpu->cs.frame_type;
	vk_buffer_put (gpu->cmdram, 4, gpu->cs.sp[i] & 0xFFFFFF, gpu->cs.pc);
	gpu->cs.sp[i] -= 4;
}

static void
pop_pc (hikaru_gpu_t *gpu)
{
	unsigned i = gpu->cs.frame_type;
	gpu->cs.sp[i] += 4;
	gpu->cs.pc = vk_buffer_get (gpu->cmdram, 4, gpu->cs.sp[i] & 0xFFFFFF) + 8;
}

static bool
is_vertex_op (uint32_t op)
{
	switch (op) {
		/* Nop */
	case 0x000:
		/* Vertex Normal */
	case 0x1B8:
	case 0x1BC:
	case 0x1BD:
	case 0xFB8:
	case 0xFBC:
	case 0xFBD:
	case 0xFBE:
	case 0xFBF:
		/* Vertex */
	case 0x12C:
	case 0x12D:
	case 0xF2C:
	case 0xF2D:
	case 0x1AC:
	case 0x1AD:
	case 0xFAC:
	case 0xFAD:
		/* Tex Coord */
	case 0xEE8:
	case 0xEE9:
		return true;
	default:
		return false;
	}
}

static void
read_inst (hikaru_gpu_t *gpu, uint32_t inst[8])
{
	hikaru_t *hikaru = (hikaru_t *) gpu->base.mach;
	vk_buffer_t *buffer = NULL;
	uint32_t mask = 0;
	unsigned i;

	switch (gpu->cs.pc >> 24) {
	case 0x40:
	case 0x41:
		buffer = hikaru->ram_s;
		mask = 0x01FFFFFF;
		break;
	case 0x48:
	case 0x4C:
		buffer = hikaru->cmdram;
		mask = 0x00FFFFFF;
		break;
	}

	VK_ASSERT (buffer != NULL);
	VK_ASSERT (mask != 0);

	for (i = 0; i < 8; i++)
		inst[i] = vk_buffer_get (buffer, 4, (gpu->cs.pc + i * 4) & mask);
}

#define ASSERT(cond_) \
	do { \
		if (!(cond_)) { \
			VK_ABORT ("GPU: @%08X: assertion failed, aborting [%08X %08X %08X %08X %08X %08X %08X %08X]", \
			          gpu->cs.pc, \
				  inst[0], inst[1], inst[2], inst[3], \
			          inst[4], inst[5], inst[6], inst[7]); \
		} \
	} while (0);

int
hikaru_gpu_exec_one (hikaru_gpu_t *gpu)
{
	uint32_t inst[8] = { 0 }, op;

	ASSERT (is_valid_addr (gpu->cs.pc));
	ASSERT (is_valid_addr (gpu->cs.sp[0]));
	ASSERT (is_valid_addr (gpu->cs.sp[1]));

	read_inst (gpu, inst);
	op = inst[0] & 0xFFF;

	switch (op) {

	/* Flow Control
	 * ============
	 *
	 * Jump, Call, Return, Kill, Sync.
	 */

	case 0x000:
		/* 000	Nop
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 */
		VK_LOG ("GPU CMD %08X: Nop [%08X]", gpu->cs.pc, inst[0]);
		ASSERT (inst[0] == 0);
		gpu->cs.pc += 4;
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
			        gpu->cs.pc, inst[0], addr);
			ASSERT (inst[0] == 0x12);
			if (addr == gpu->cs.pc) {
				VK_ERROR ("self-jump, skipping CS");
				return 1;
			}
			gpu->cs.pc = addr;
		}
		break;
	case 0x812:
		/* 812	Jump Rel
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa		a = Offset in 32-bit words
		 */
		{
			uint32_t addr = gpu->cs.pc + inst[1] * 4;
			VK_LOG ("GPU CMD %08X: Jump Rel [%08X %08X] %08X",
			        gpu->cs.pc, inst[0], inst[1], addr);
			ASSERT (inst[0] == 0x812);
			if (addr == gpu->cs.pc) {
				VK_ERROR ("self-jump, skipping CS");
				return 1;
			}
			gpu->cs.pc = addr;
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
			        gpu->cs.pc, inst[0], addr);
			ASSERT (inst[0] == 0x52);
			if (addr == gpu->cs.pc) {
				VK_ERROR ("self-call, skipping CS");
				return 1;
			}
			push_pc (gpu);
			gpu->cs.pc = addr;

		}
		break;
	case 0x852:
		/* 852	Call Rel
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa		a = Offset in 32-bit words
		 */
		{
			uint32_t addr = gpu->cs.pc + inst[1] * 4;
			VK_LOG ("GPU CMD %08X: Jump Rel [%08X %08X] %08X",
			        gpu->cs.pc, inst[0], inst[1], addr);
			ASSERT (inst[0] == 0x852);
			if (addr == gpu->cs.pc) {
				VK_ERROR ("self-call, skipping CS");
				return 1;
			}
			push_pc (gpu);
			gpu->cs.pc = addr;
		}
		break;
	case 0x082:
		/* 082	Return
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 */
		VK_LOG ("GPU CMD %08X: Return [%08X]",
		        gpu->cs.pc, inst[0]);
		ASSERT (inst[0] == 0x82);
		pop_pc (gpu);
		break;
	case 0x1C2:
		/* 1C2	Kill
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 */
		VK_LOG ("GPU CMD %08X: Kill [%08X]",
		        gpu->cs.pc, inst[0]);
		ASSERT (inst[0] == 0x1C2);
		gpu->cs.is_running = false;
		gpu->cs.pc += 4;
		return 1;
	case 0x781:
		/* 781	Sync
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
			        gpu->cs.pc, inst[0], a, b, n, m);

			gpu->cs.pc += 4;
		}
		break;

	/* Clear Primitives
	 * ================
	 *
	 * No idea.
	 */

	case 0x154:
		/* 154	Commit Alpha Threshold
		 *
		 *	---- ---- --nn nnnn ---- oooo oooo oooo
		 *	hhhh hhhh hhhh hhhh hhhh hhhh llll llll
		 *
		 * n = Unknown
		 * l = Alpha low threshold
		 * h = Alpha high threshold
		 *
		 * See PH:@0C017798, PH:@0C0CF868. It may be related to
		 * command C81, see PH:@0C0CF872 and PH:@0C0CF872.
		 */
		{
			unsigned n = (inst[0] >> 16) & 0x3F;
			int32_t thresh_lo = inst[1] & 0xFF;
			int32_t thresh_hi = signext_n_32 ((inst[1] >> 8), 23);

			VK_LOG ("GPU CMD %08X: Commit Alpha Threshold [%08X %08X] %u <%X %X>",
			        gpu->cs.pc, inst[0], inst[1],
			        n, thresh_lo, thresh_hi);

			gpu->cs.pc += 8;
		}
		break;
	case 0x194:
		/* 194	Commit Ramp Data
		 *
		 *	nnnn nnnn mmmm mm-- ---- oooo oooo oooo
		 *	aaaa aaaa aaaa aaaa bbbb bbbb bbbb bbbb
		 *
		 * NOTE: these come in groups of 8. The data for each group
		 * comes from a different ptr.
		 *
		 * See PH:@0C017A3E.
		 */
		{
			unsigned n, m, a, b;
			n = (inst[0] >> 24) & 0xFF;
			m = (inst[0] >> 19) & 0x1F;
			a = inst[1] & 0xFFFF;
			b = inst[1] >> 16;

			VK_LOG ("GPU CMD %08X: Commit Ramp Data [%08X %08X] %u %u <%X %X>",
			        gpu->cs.pc, inst[0], inst[1], n, m, a, b);

			gpu->cs.pc += 8;
		}
		break;

	/* Viewport
	 * ========
	 *
	 * The parameters for many of these are taken from PHARRIER debug
	 * info.
	 */

	case 0x021:
		/* 021	Set Projection
		 *
		 * 	---- ---- ---- ---- ---- oooo oooo oooo
		 *      pppp pppp pppp pppp pppp pppp pppp pppp
		 *      qqqq qqqq qqqq qqqq qqqq qqqq qqqq qqqq
		 *      zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *
		 * p = (display height / 2) * tanf (angle / 2)
		 * q = (display height / 2) * tanf (angle / 2)
		 * z = depth kappa (used also in 621)
		 *
		 * See PH:@0C01587C, PH:@0C0158A4, PH:@0C0158E8.
		 */
		gpu->viewports.scratch.persp_x		= *(float *) &inst[1];
		gpu->viewports.scratch.persp_y		= *(float *) &inst[2];
		gpu->viewports.scratch.persp_unk	= *(float *) &inst[3];

		VK_LOG ("GPU CMD %08X: Viewport: Set Projection [%08X %08X %08X %08X] px=%f py=%f unk=%f",
		        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3],
		        gpu->viewports.scratch.persp_x,
		        gpu->viewports.scratch.persp_y,
		        gpu->viewports.scratch.persp_unk);

		gpu->cs.pc += 16;
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
		gpu->viewports.scratch.center.x[0]	= inst[1] & 0xFFFF;
		gpu->viewports.scratch.center.x[1]	= inst[1] >> 16;
		gpu->viewports.scratch.extents_x.x[0]	= inst[2] & 0x7FFF;
		gpu->viewports.scratch.extents_x.x[1]	= inst[3] & 0x7FFF;
		gpu->viewports.scratch.extents_y.x[0]	= (inst[2] >> 16) & 0x3FFF;
		gpu->viewports.scratch.extents_y.x[1]	= (inst[3] >> 16) & 0x3FFF;

		VK_LOG ("GPU CMD %08X: Viewport: Set Extents [%08X %08X %08X %08X] center=<%u,%u> x=<%u,%u> y=<%u,%u> ]",
		        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3],
		        gpu->viewports.scratch.center.x[0],
		        gpu->viewports.scratch.center.x[1],
		        gpu->viewports.scratch.extents_x.x[0],
		        gpu->viewports.scratch.extents_x.x[1],
		        gpu->viewports.scratch.extents_y.x[0],
		        gpu->viewports.scratch.extents_y.x[1])

		ASSERT (!(inst[2] & 0xC0008000));
		ASSERT (!(inst[3] & 0xC0008000));

		gpu->cs.pc += 16;
		break;
	case 0x421:
		/* 421	Viewport: Set Depth Range
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 *	nnnn nnnn nnnn nnnn nnnn nnnn nnnn nnnn
		 *	ffff ffff ffff ffff ffff ffff ffff ffff
		 *	FFF- ---- ---- ---- ---- ---- ---- ----
		 *
		 * n = depth near; also called depth kappa, used in 021 and 621
		 * f = depth far
		 * F = depth function?
		 *
		 * See PH:@0C015AA6
		 */
		gpu->viewports.scratch.depth_near = *(float *) &inst[1];
		gpu->viewports.scratch.depth_far  = *(float *) &inst[2];
		gpu->viewports.scratch.depth_func = inst[3] >> 29;

		VK_LOG ("GPU CMD %08X: Viewport: Set Depth Range [%08X %08X %08X %08X] func=%u near=%f far=%f",
		        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3],
		        gpu->viewports.scratch.depth_func,
		        gpu->viewports.scratch.depth_near,
		        gpu->viewports.scratch.depth_far);
			        
		ASSERT (!(inst[3] & 0x1FFFFFFF));

		gpu->cs.pc += 16;
		break;
	case 0x621:
		/* 621	Viewport: Set Depth Queue
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
		 *     stored in (13, GBR) aka 0C00F034; it is also the third
		 *     parameter of instruction 021.
		 *
		 * See PH:@0C0159C4, PH:@0C015A02, PH:@0C015A3E.
		 */
		gpu->viewports.scratch.depthq_type	= (inst[0] >> 18) & 3;
		gpu->viewports.scratch.depthq_enabled	= ((inst[0] >> 17) & 1) ? 0 : 1;
		gpu->viewports.scratch.depthq_unk	= (inst[0] >> 16) & 1;
		gpu->viewports.scratch.depthq_mask.x[0]	= inst[1] & 0xFF;
		gpu->viewports.scratch.depthq_mask.x[1]	= (inst[1] >> 8) & 0xFF;
		gpu->viewports.scratch.depthq_mask.x[2]	= (inst[1] >> 16) & 0xFF;
		gpu->viewports.scratch.depthq_mask.x[3]	= inst[1] >> 24;
		gpu->viewports.scratch.depthq_density	= *(float *) &inst[2];
		gpu->viewports.scratch.depthq_bias	= *(float *) &inst[3];

		VK_LOG ("GPU CMD %08X: Viewport: Set Depth Queue [%08X %08X %08X %08X] type=%u enabled=%u unk=%u mask=<%X %X %X %X> density=%f bias=%f",
		        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3],
			gpu->viewports.scratch.depthq_type,
			gpu->viewports.scratch.depthq_enabled,
			gpu->viewports.scratch.depthq_unk,
			gpu->viewports.scratch.depthq_mask.x[0],
			gpu->viewports.scratch.depthq_mask.x[1],
			gpu->viewports.scratch.depthq_mask.x[2],
			gpu->viewports.scratch.depthq_mask.x[3],
			gpu->viewports.scratch.depthq_density,
			gpu->viewports.scratch.depthq_bias);

		gpu->cs.pc += 16;
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

			gpu->viewports.scratch.ambient_color = color;

			VK_LOG ("GPU CMD %08X: Viewport: Set Ambient Color [%08X %08X] color=<%u %u %u>",
			        gpu->cs.pc, inst[0], inst[1],
			        color.x[2], color.x[1], color.x[0]);

			gpu->cs.pc += 8;
		}
		break;
	case 0x991:
		/* 991	Viewport: Set Clear Color
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo		o = Opcode
		 *	---- ---a gggg gggg bbbb bbbb rrrr rrrr		a,r,g,b = Clear color
		 *
		 * NOTE: yes, apparently blue and green _are_ swapped.
		 *
		 * XXX double check the alpha mask.
		 *
		 * See PH:@0C016368, PH:@0C016396, PH:@0C037760.
		 */
		gpu->viewports.scratch.clear_color.x[0] = inst[1] & 0xFF;
		gpu->viewports.scratch.clear_color.x[1] = (inst[1] >> 8) & 0xFF;
		gpu->viewports.scratch.clear_color.x[2] = (inst[1] >> 16) & 0xFF;
		gpu->viewports.scratch.clear_color.x[3] = ((inst[1] >> 24) & 1) ? 0xFF : 0;

		VK_LOG ("GPU CMD %08X: Viewport: Set Clear Color [%08X %08X] color=<%u %u %u %u>",
		        gpu->cs.pc, inst[0], inst[1],
		        gpu->viewports.scratch.clear_color.x[0],
		        gpu->viewports.scratch.clear_color.x[1],
		        gpu->viewports.scratch.clear_color.x[2],
		        gpu->viewports.scratch.clear_color.x[3]);

		gpu->cs.pc += 8;
		break;
	case 0x004:
		/* 004	Commit Viewport
		 *
		 *	---- ---- ---- nnnn ---- oooo oooo oooo
		 *
		 * n = Num
		 *
		 * See PH:@0C015AD0.
		 */
		{
			unsigned n = (inst[0] >> 16) & VIEWPORT_MASK;

			gpu->viewports.table[n] = gpu->viewports.scratch;

			VK_LOG ("GPU CMD %08X: Commit Viewport [%08X] %u",
			        gpu->cs.pc, inst[0], n);

			gpu->cs.pc += 4;
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

			hikaru_renderer_set_viewport (gpu->renderer,
			                              &gpu->viewports.table[n]);

			VK_LOG ("GPU CMD %08X: Recall Viewport [%08X] %u",
			        gpu->cs.pc, inst[0], n);

			gpu->cs.pc += 4;
		}
		break;

	/* Material
	 * ========
	 *
	 * The parameters for many of these are taken from PHARREIR debug
	 * info.
	 *
	 * XXX map shading and blending mode. See pharrier-m.dis
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
			unsigned i = (op >> 9) & 1;

			gpu->materials.scratch.color[i].x[0] = inst[1] & 0xFF;
			gpu->materials.scratch.color[i].x[1] = (inst[1] >> 8) & 0xFF;
			gpu->materials.scratch.color[i].x[2] = (inst[1] >> 16) & 0xFF;

			VK_LOG ("GPU CMD %08X: Material: Set Color %X [%08X %08X] <R=%u G=%u B=%u>",
			        gpu->cs.pc, i, inst[0], inst[1],
			        gpu->materials.scratch.color[i].x[0],
			        gpu->materials.scratch.color[i].x[1],
			        gpu->materials.scratch.color[i].x[2]);

			gpu->cs.pc += 8;
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
			uint8_t specularity;
			vec3b_t shininess;

			shininess.x[0] = inst[1] & 0xFF;
			shininess.x[1] = (inst[1] >> 8) & 0xFF;
			shininess.x[2] = (inst[1] >> 16) & 0xFF;

			specularity = inst[1] >> 24;

			gpu->materials.scratch.shininess = shininess;
			gpu->materials.scratch.specularity = specularity;

			VK_LOG ("GPU CMD %08X: Material: Set Shininess [%08X %08X] specularity=%u shininess=<%u %u %u>",
			        gpu->cs.pc, inst[0], inst[1],
			        specularity, shininess.x[2],
			        shininess.x[1], shininess.x[0]);

			gpu->cs.pc += 8;
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

			gpu->materials.scratch.material_color = color;

			VK_LOG ("GPU CMD %08X: Material: Set Material Color [%08X %08X] color=<%u %u %u>",
			        gpu->cs.pc, inst[0], inst[1],
			        color.x[0], color.x[1], color.x[2]);

			gpu->cs.pc += 8;
		}
		break;
	case 0x081:
		/* 081	Material: Set Unknown
		 *
		 *	---- ---- ---- mmmm ---n oooo oooo oooo
		 *
		 * No code reference available.
		 */
		{
			unsigned n = (inst[0] >> 12) & 1;
			unsigned m = (inst[0] >> 16) & 0xF;

			VK_LOG ("GPU CMD %08X: Material: Set Unknown [%08X] %u %u",
			        gpu->cs.pc, inst[0], n, m);

			gpu->cs.pc += 4;
		}
		break;
	case 0x881:
		/* 881	Set Flags
		 *
		 *	---- ---- --ha tzmm ---- oooo oooo oooo
		 *
		 * m = Mode (Shading Mode?)
		 * z = Depth blend (fog)
		 * t = Enable texture
		 * a = Alpha mode
		 * h = Highlight mode
		 *
		 * m should include Flat, Linear, Phong.
		 *
		 * See PH:@0C0CF700.
		 */
		gpu->materials.scratch.mode		= (inst[0] >> 16) & 3;
		gpu->materials.scratch.depth_blend	= (inst[0] >> 18) & 1;
		gpu->materials.scratch.has_texture	= (inst[0] >> 19) & 1;
		gpu->materials.scratch.has_alpha	= (inst[0] >> 20) & 1;
		gpu->materials.scratch.has_highlight	= (inst[0] >> 21) & 1;

		VK_LOG ("GPU CMD %08X: Material: Set Flags [%08X] mode=%u zblend=%u tex=%u alpha=%u highl=%u",
		        gpu->cs.pc, inst[0],
			gpu->materials.scratch.mode,
			gpu->materials.scratch.depth_blend,
			gpu->materials.scratch.has_texture,
			gpu->materials.scratch.has_alpha,
			gpu->materials.scratch.has_highlight);

		gpu->cs.pc += 4;
		break;
	case 0xA81:
		/* A81	Material: Set BMode
		 *
		 *	---- ---- ---- ---mm ---- oooo ooo oooo
		 *
		 * Blending Mode?
		 *
		 * See PH:@0C0CF7FA.
		 */
		gpu->materials.scratch.bmode = (inst[0] >> 16) & 3;

		VK_LOG ("GPU CMD %08X: Material: Set BMode [%08X]", gpu->cs.pc, inst[0]);

		gpu->cs.pc += 4;
		break;
	case 0xC81:
		/* C81	Material: Set Unknown
		 *
		 *	---- ---- --xu uuuu ---- oooo oooo oooo
		 *
		 * x,u = Unknown
		 *
		 * It may be related to command 154, see PH:@0C0CF872 and
		 * PH:@0C0CF872.
		 */

		VK_LOG ("GPU CMD %08X: Material: Set Unknown [%08X]", gpu->cs.pc, inst[0]);

		gpu->cs.pc += 4;
		break;
	case 0x084:
		/* 084	Commit Material
		 *
		 *	---- ---- nnnn nnnn ---u oooo oooo oooo
		 *
		 * n = Index
		 * u = Unknown
		 *
		 * See PH:@0C0153D4, PH:@0C0CF878.
		 */
		{
			unsigned n = (inst[0] >> 16) & MATERIAL_MASK;
			unsigned u = (inst[0] >> 12) & 1;

			VK_LOG ("GPU CMD %08X: Commit Material [%08X] num=%u unk=%u",
			        gpu->cs.pc, inst[0], n, u);

			gpu->cs.pc += 4;

			n += gpu->materials.offset;
			if (n >= NUM_MATERIALS) {
				VK_ERROR ("GPU MATERIAL: commit index exceeds MAX (%u >= %u), skipping",
				          n, NUM_MATERIALS);
				break;
			}
			n &= MATERIAL_MASK;
			gpu->materials.table[n] = gpu->materials.scratch;
		}
		break;
	case 0x083:
		/* 083	Recall Material
		 *
		 *	uuuu uuuu nnnn nnnn ---e oooo oooo oooo
		 *
		 * u = Unknown
		 * n = Index
		 * e = Enable
		 *
		 * See @0C00657C, PH:@0C0CF882.
		 */
		{
			unsigned n = inst[0] >> 16;
			unsigned e = (inst[0] >> 12) & 1;

			VK_LOG ("GPU CMD %08X: Recall Material [%08X] (%s) num=%u ena=%u",
			        gpu->cs.pc, inst[0],
			        e ? "" : "OFFS ONLY", n, e);

			gpu->cs.pc += 4;

			if (e) {
				n += gpu->materials.offset;
				if (n >= NUM_MATERIALS) {
					VK_ERROR ("GPU MATERIAL: recall index exceeds MAX (%u >= %u), skipping",
					          n, NUM_MATERIALS);
					break;
				}
				n &= MATERIAL_MASK;
				hikaru_renderer_set_material (gpu->renderer,
				                              &gpu->materials.table[n]);
			} else
				gpu->materials.offset = n;
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
		 *	---- mmmm mmmm nnnn ---- oooo oooo oooo
		 *
		 * m, n = Unknown
		 *
		 * See PH:@0C015B7A.
		 */
		gpu->texheads.scratch._0C1_nibble	= (inst[0] >> 16) & 0xF;
		gpu->texheads.scratch._0C1_byte		= (inst[0] >> 20) & 0xFF;

		VK_LOG ("GPU CMD %08X: Texhead: Set Unknown [%08X]",
		        gpu->cs.pc, inst[0]);

		gpu->cs.pc += 4;
		break;
	case 0x2C1:
		/* 2C1	Texhead: Set Format/Size
		 *
		 *	888F FFll llHH HWWW uu-- oooo oooo oooo
		 *
		 * 8 = argument on stack
		 * F = Format (argument R7)
		 * H = log16 of Height (argument R6)
		 * l = lower four bits of argument R4
		 * W = log16 of Width (argument R5)
		 * u = Upper two bits of argument R4
		 *
		 * NOTE: the parameters are also used in conjunction with
		 * the GPU IDMA-like device. It has the very same format
		 * as the third word.
		 *
		 * See PH:@0C015BCC
		 */
		gpu->texheads.scratch._2C1_unk4	=
			(((inst[0] >> 14) & 3) << 4) |
			((inst[0] >> 22) & 15);
		gpu->texheads.scratch.width	= 16 << ((inst[0] >> 16) & 7);
		gpu->texheads.scratch.height	= 16 << ((inst[0] >> 19) & 7);
		gpu->texheads.scratch.format	= (inst[0] >> 26) & 7;
		gpu->texheads.scratch._2C1_unk8	= inst[0] >> 29;

		VK_LOG ("GPU CMD %08X: Texhead: Set Format/Size [%08X]",
		        gpu->cs.pc, inst[0]);

		gpu->cs.pc += 4;
		break;
	case 0x4C1:
		/* 4C1	Texhead: Set Slot
		 *
		 *	nnnn nnnn mmmm mmmm ----b oooo oooo oooo
		 *
		 * n = Slot Y
		 * m = Slot X
		 * b = AUXTEXRAM bank
		 *
		 * NOTE: the parameters are also used in conjunction with
		 * the GPU IDMA-like device.
		 *
		 * See PH:@0C015BA0.
		 */
		gpu->texheads.scratch.bank = (inst[0] >> 12) & 1;
		gpu->texheads.scratch.slotx = (inst[0] >> 16) & 0xFF;
		gpu->texheads.scratch.sloty = inst[0] >> 24;

		VK_LOG ("GPU CMD %08X: Texhead: Set Slot [%08X]",
		        gpu->cs.pc, inst[0]);

		gpu->cs.pc += 4;
		break;
	case 0x0C4:
		/* 0C4	Commit Texhead
		 *
		 *	 ---- ---- nnnn nnnn ---e oooo oooo oooo
		 *
		 * n = Index
		 * u = Unknown
		 *
		 * See PH:@0C01545C. 
		 */
		{
			unsigned n = inst[0] >> 16;
			unsigned e = (inst[0] >> 12) & 1;

			VK_LOG ("GPU CMD %08X: Commit Texhead [%08X] n=%u e=%u",
			        gpu->cs.pc, inst[0], n, e);

			gpu->cs.pc += 4;

			n += gpu->texheads.offset;
			if (n >= NUM_TEXHEADS) {
				VK_ERROR ("GPU TEXHEAD: commit index exceeds MAX: %u >= %u",
				          n, NUM_TEXHEADS);
				break;
			}
			n &= TEXHEAD_MASK;
			gpu->texheads.table[n] = gpu->texheads.scratch;
		}
		break;
	case 0x0C3:
		/* 0C3	Recall Texhead
		 *
		 *	nnnn nnnn nnnn nnnn ---e oooo oooo oooo
		 *
		 * n = index
		 * e = bind if 1, set offset if 0
		 */
		{
			unsigned n = inst[0] >> 16;
			unsigned e = (inst[0] >> 12) & 1;

			VK_LOG ("GPU CMD %08X: Recall Texhead [%08X] (%s) n=%u e=%u",
			        gpu->cs.pc, inst[0],
			        e ? "" : "OFFS ONLY", n, e);

			gpu->cs.pc += 4;

			if (e) {
				n += gpu->texheads.offset;
				if (n >= NUM_TEXHEADS) {
					VK_ERROR ("GPU TEXHEAD: bind index exceeds MAX (%u >= %u), skipping",
					          n, NUM_TEXHEADS);
					break;
				}
				n &= TEXHEAD_MASK;
				hikaru_renderer_set_texhead (gpu->renderer,
				                             &gpu->texheads.table[n]);
			} else
				gpu->texheads.offset = n;
		}
		break;

	/* Lighting Operations
	 * ===================
	 *
	 * Just random notes, really. However the system16.com specs say:
	 * "four lights per polygon, 1024 lights total."
	 */

	case 0x261:
		/* 261	Set Light Type/Extents
		 *
		 *	---- ---- ---- --tt ---- oooo oooo oooo
		 *	pppp pppp pppp pppp pppp pppp pppp pppp
		 *	qqqq qqqq qqqq qqqq qqqq qqqq qqqq qqqq
		 *	rrrr rrrr rrrr rrrr rrrr rrrr rrrr rrrr
		 *
		 * t = Light type
		 * p = Unknown
		 * q = Unknown
		 * r = Ignored; it's non-zero because the upload functions
		 *     only set words 0 to 2 without clearing word 3.
		 *
		 * Type 0:
		 *
		 *  p = 
		 *  q = 
		 *
		 * Type 1:
		 *
		 *  p = 
		 *  q = 
		 *
		 * Type 2:
		 *
		 *  p = 
		 *  q = 
		 *
		 * Type 3:
		 *
		 *  p = (hi^2 * lo^2) / (hi^2 - lo^2)
		 *  q = 1.0 / sqrt (hi^4)
		 *
		 * NOTE: light types according to the PHARRIER text are:
		 * constant, infinite, square, reciprocal, reciprocal2,
		 * linear.
		 */
		{
			unsigned type = (inst[0] >> 16) & 3;
			float p = *(float *) &inst[1];
			float q = *(float *) &inst[2];

			VK_LOG ("GPU CMD %08X: Light: Set Type/Extents [%08X %08X %08X %08X] type=%u p=%f q=%f",
			        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3],
			        type, p, q);

			gpu->cs.pc += 16;
		}
		break;
	case 0x961:
		/*
		 * 961	Set Light Position
		 *
		 *	---- ---- ---- ---e nnnn oooo oooo oooo
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *	zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *
		 * e = Unknown
		 * n = Unknown
		 * x,y,z = Light position; 7FC00000 = INF?
		 */
		{
			vec3f_t *v = (vec3f_t *) &inst[1];

			VK_LOG ("GPU CMD %08X: Light: Set Position [%08X %08X %08X %08X] <%f %f %f>",
			        gpu->cs.pc,
			        inst[0], inst[1], inst[2], inst[3],
			        v->x[0], v->x[1], v->x[2]);

			gpu->cs.pc += 16;
		}
		break;
	case 0xB61:
		/*
		 * B61	Set Light Direction
		 *
		 *	---- ---- ---- ---- nnnn oooo oooo oooo
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *	zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *
		 * n = Unknown
		 * x,y,z = Light direction
		 */
		{
			vec3f_t *v = (vec3f_t *) &inst[1];

			VK_LOG ("GPU CMD %08X: Light: Set Direction [%08X %08X %08X %08X] <%f %f %f>",
			        gpu->cs.pc,
			        inst[0], inst[1], inst[2], inst[3],
			        v->x[0], v->x[1], v->x[2]);

			gpu->cs.pc += 16;
		}
		break;
	case 0x051:
		/* 051	Light: Set Unknown
		 *
		 *	---- ---- nnnn nnnn ---- oooo oooo oooo
		 *	--aa aaaa aaaa bbbb bbbb bbcc cccc cccc
		 *
		 * n = Index?
		 * a, b, c = Color? It's three FP values * 255.0f and then
		 *           truncated and clamped to [0,FF].
		 *
		 * See PH:@0C0178C6; for a,b,c computation see PH:@0C03DC66.
		 */
		{
			vec3s_t param;
			unsigned n;

			n = (inst[0] >> 16) & 0xFF;
			param.x[0] = inst[1] & 0x3FF;
			param.x[1] = (inst[1] >> 10) & 0x3FF;
			param.x[2] = (inst[1] >> 20) & 0x3FF;

			VK_LOG ("GPU CMD %08X: Light: Set Unknown [%08X %08X] num=%u param=<%u %u %u>",
			        gpu->cs.pc, inst[0], inst[1],
			        n, param.x[2], param.x[1], param.x[0]);

			gpu->cs.pc += 8;
		}
		break;
	case 0x451:
		/* 451	Light: Set Unknown
		 *
		 *	---- ---u ---- ---- ---- oooo oooo oooo
		 *	---- ---- ---- ---- ---- ---- ---- ----
		 *
		 * u = Unknown
		 *
		 * See PH:@0C017A7C, PH:@0C017B6C, PH:@0C017C58,
		 * PH:@0C017CD4, PH:@0C017D64, 
		 */

		VK_LOG ("GPU CMD %08X: Light: Set Unknown %03X [%08X %08X]",
		        gpu->cs.pc, inst[0] & 0xFFF, inst[0], inst[1]);

		gpu->cs.pc += 8;
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
		        gpu->cs.pc, inst[0] & 0xFFF, inst[0], inst[1], inst[2], inst[3]);
		gpu->cs.pc += 16;
		break;
	case 0x043:
		/* 043	Recall Lightset
		 *
		 *	---- mmmm ---- nnnn ---e oooo oooo oooo
		 *
		 * m,n = Unknown (enable bitmasks?)
		 *
		 * NOTE: it's always used _before_ any lighting stuff is
		 * uploaded.
		 */
		{
			unsigned u = (inst[0] >> 24) & 0xF;
			unsigned n = (inst[0] >> 16) & 0xF;
			unsigned e = (inst[0] >> 12) & 1;

			if (e) {
				n += gpu->lights.offset;
				hikaru_renderer_set_light (gpu->renderer,
				                           &gpu->lights.table[n & LIGHT_MASK]);
			} else
				gpu->lights.offset = n;

			VK_LOG ("GPU CMD %08X: Recall Lightset [%08X] n=%u u=%u",
			        gpu->cs.pc, inst[0], n, u);

			gpu->cs.pc += 4;
		}
		break;
	case 0x006:
		/* 006	Light: Unknown
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo	o = Opcode
		 */
		VK_LOG ("GPU CMD %08X: Light: Unknown %03X [%08X]",
		        gpu->cs.pc, inst[0] & 0xFFF, inst[0]);
		gpu->cs.pc += 4;
		break;
	case 0x046:
		/* 046	Light: Unknown
		 *
		 *	---- ---- ---- ---n ---- oooo oooo oooo	o = Opcode, n = Unknown
		 */
		VK_LOG ("GPU CMD %08X: Light: Unknown %03X [%08X]",
		        gpu->cs.pc, inst[0] & 0xFFF, inst[0]);
		gpu->cs.pc += 4;
		break;
	case 0x104:
		/* 104	Commit Light
		 *
		 *	---- ---- ---n nnnn ---- oooo oooo oooo
		 *
		 * n = Index
		 */
		{
			unsigned n = (inst[0] >> 16) & MATRIX_MASK;

			VK_LOG ("GPU CMD %08X: Commit Light [%08X] %u",
			        gpu->cs.pc, inst[0], n);

			gpu->cs.pc += 4;
		}
		break;
	case 0x064:
		/* 064  Commit Lightset
		 *
		 *      ---- ---- ---- nnnn ---e oooo oooo oooo
		 *      bbbb bbbb bbbb bbbb aaaa aaaa aaaa aaaa
		 *      dddd dddd dddd dddd cccc cccc cccc cccc
		 *      ---- ---- ---- ---- ---- ---- ---- ----
		 *
		 * n = Unknown
		 * e = Unknown
		 * a,b,c,d = indices of four light vectors
		 *
		 * See PH:@0C017DF0.
		 */
		{
			VK_LOG ("GPU CMD %08X: Commit Lightset %03X [%08X %08X %08X %08X]",
			        gpu->cs.pc, inst[0] & 0xFFF, inst[0], inst[1], inst[2], inst[3]);

			gpu->cs.pc += 16;
		}
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
		 *	---- ---- ---- mmnn ---- oooo oooo oooo
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *	zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *
		 * m = Matrix index?
		 * n = Vector index
		 * x,y,z = Vector elements
		 *
		 * NOTE: bit 4 of n becomes bit 3 in PH:@0C015CF2. This is
		 * odd. It may highlight a 'set-offset' effect for matrix
		 * commands too.
		 *
		 * See @0C008080.
		 */
		{
			unsigned m = (inst[0] >> 18) & 3;
			unsigned n = (inst[0] >> 16) & 3;
			vec3f_t *v = (vec3f_t *) &inst[1];

			VK_LOG ("GPU CMD %08X: Matrix: Vector [%08X %08X %08X %08X] %u %u <%f %f %f>",
			        gpu->cs.pc,
			        inst[0], inst[1], inst[2], inst[3],
			        n, m,
			        v->x[0], v->x[1], v->x[2]);

			hikaru_renderer_set_modelview_vertex (gpu->renderer,
			                                      n, m, v);

			gpu->cs.pc += 16;
		}
		break;

	/* Vertex Operations
	 * =================
	 *
	 * This class of instructions pushes (or otherwise deals with) vertex
	 * data to the transformation/rasterization pipeline.
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
	 * XXX there are at least two other vertex-related instructions: 12C
	 * and 15C.
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
		 *      pppp pppp mmmm nnnn qqqq oooo oooo oooo
		 *      xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
		 *      yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *      zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *      ssss ssss ssss ssss tttt tttt tttt tttt
		 *      uuuu uuuu uuuu uuuu uuuu uuuu uuuu uuuu
		 *      vvvv vvvv vvvv vvvv vvvv vvvv vvvv vvvv
		 *      wwww wwww wwww wwww wwww wwww wwww wwww
		 *
		 * x,y,z = Vertex position
		 * u,v,w = Vertex normal
		 * s,t = Vertex texcoords
		 * n,m,p,q = Unknown
		 * 1 vs F = Unknown
		 * 8 vs 9 vs ... vs F = Unknown
		 */
		{
			unsigned n, m, p, q;
			vec3f_t *pos, *nrm;
			vec2f_t texcoords;

			p = inst[0] >> 24;
			n = (inst[0] >> 20) & 15;
			m = (inst[0] >> 16) & 15;
			q = (inst[0] >> 12) & 15;

			pos = (vec3f_t *) &inst[1];
			nrm = (vec3f_t *) &inst[5];

			texcoords.x[0] = (inst[4] & 0xFFFF) / 16.0f;
			texcoords.x[1] = (inst[4] >> 16) / 16.0f;

			VK_LOG ("GPU CMD %08X: Vertex Normal [%08X %08X %08X %08X %08X %08X %08X %08X] <%f %f %f> <%f %f %f> <%f %f> %u %u %u %u",
			        gpu->cs.pc,
				inst[0], inst[1], inst[2], inst[3],
				inst[4], inst[5], inst[6], inst[7],
			        pos->x[0], pos->x[1], pos->x[2],
			        nrm->x[0], nrm->x[1], nrm->x[2],
				texcoords.x[0], texcoords.x[1],
			        n, m, p, q);

			hikaru_renderer_append_vertex_full (gpu->renderer,
			                                    pos, nrm, &texcoords);

			gpu->cs.pc += 32;
		}
		break;
	case 0x1AC:
	case 0x1AD:
	case 0xFAC:
	case 0xFAD:
		/* xAC	Vertex
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *	zzzz zzzz zzzz zzzz zzzz zzzz zzzz zzzz
		 *
		 * x,y,z = Vertex position
		 */
		{
			vec3f_t pos;

			pos.x[0] = *(float *) &inst[1];
			pos.x[1] = *(float *) &inst[2];
			pos.x[2] = *(float *) &inst[3];

			VK_LOG ("GPU CMD %08X: Vertex [%08X] { %f %f %f }",
			        gpu->cs.pc, inst[0],
			        pos.x[0], pos.x[1], pos.x[2]);

			hikaru_renderer_append_vertex (gpu->renderer, &pos);

			gpu->cs.pc += 16;
		}
		break;
	case 0xEE8:
	case 0xEE9:
		/* EE9	Tex Coord
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 *	???? yyyy yyyy yyyy ???? xxxx xxxx xxxx
		 *	???? yyyy yyyy yyyy ???? xxxx xxxx xxxx
		 *	???? yyyy yyyy yyyy ???? xxxx xxxx xxxx
		 *
		 * x,y = Tex coords for vertex W; where W is the word index.
		 */
		{
			vec2f_t texcoords[3];
			unsigned i;

			for (i = 0; i < 3; i++) {
				texcoords[i].x[0] = (inst[i+1] & 0xFFFF) / 16.0f;
				texcoords[i].x[1] = (inst[i+1] >> 16) / 16.0f;
			}

			VK_LOG ("GPU CMD %08X: Tex Coord [%08X %08X %08X %08X] <%f %f> <%f %f> <%f %f>",
			        gpu->cs.pc,
			        inst[0], inst[1], inst[2], inst[3],
			        texcoords[0].x[0], texcoords[0].x[1],
			        texcoords[1].x[0], texcoords[1].x[1],
			        texcoords[2].x[0], texcoords[2].x[1]);

			hikaru_renderer_append_texcoords (gpu->renderer, texcoords);

			gpu->cs.pc += 16;
		}
		break;
	case 0x12C:
	case 0x12D:
	case 0x12E:
	case 0x12F:
	case 0xF2C:
	case 0xF2D:
	case 0xF2E:
	case 0xF2F:
		/* 12C	Vertex Unknown
		 *
		 *	wwww wwww ---- ---- jjjj oooo oooo oooo
		 *	xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
		 *	vvvv vvvv vvvv vvvv uuuu uuuu uuuu uuuu
		 *	yyyy yyyy yyyy yyyy yyyy yyyy yyyy yyyy
		 *
		 * w,j = Unknown
		 * x,z = Unknown fp
		 * u,v = Unknown int16_t
		 */
		{
			float x = *(float *) &inst[1];
			float z = *(float *) &inst[3];
			int32_t u = (int32_t)(int16_t)(inst[2] & 0xFFFF);
			int32_t v = (int32_t)(int16_t)(inst[2] >> 16);

			VK_LOG ("GPU CMD %08X: Vertex Unk [%08X %08X %08X %08X] { %f %d %d %f }",
			        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3],
			        x, v, u, z);

			gpu->cs.pc += 16;
		}
		break;
	case 0x158:
	case 0x159:
	case 0x15A:
	case 0x15B:
	case 0xF58:
	case 0xF59:
	case 0xF5A:
	case 0xF5B:
		/* 158	Tex Coord Unknown
		 *
		 *	---- ---- u--- ---- ---- oooo oooo oooo
		 *	vvvv vvvv vvvv vvvv uuuu uuuu uuuu uuuu
		 *
		 * u = Unknown
		 * v = Tex coords for the previous 12C instruction?
		 */
		{
			uint16_t u = inst[1] & 0xFFFF;
			uint16_t v = inst[1] >> 16;

			VK_LOG ("GPU CMD %08X: Tex Coord Unk [%08X %08X] { %u %u }",
			        gpu->cs.pc, inst[0], inst[1], u, v);

			gpu->cs.pc += 8;
		}
		break;

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
			        gpu->cs.pc, inst[0], u);
			gpu->cs.pc += 4;
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
			        gpu->cs.pc, inst[0] & 0xFFF, inst[0], n);
			gpu->cs.pc += 4;
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
		 * See PH:@0C0173CA. For evidence that these commands are
		 * related, see PH:@0C0EEFBE.
		 */
		{
			unsigned f = inst[0] >> 24;
			unsigned n = ((inst[0] >> 8) & 0xF) - 1;

			VK_LOG ("GPU CMD %08X: Recall %03X [%08X] f=%X n=%X",
			        gpu->cs.pc, inst[0] & 0xFFF, inst[0], f, n);

			gpu->cs.pc += 4;
		}
		break;
	case 0x313:
	case 0xD13:
		/* These could be just like the above; no idea. */
		{
			unsigned u = inst[0] >> 24;

			VK_LOG ("GPU CMD %08X: Recall %03X [%08X] %u",
			        gpu->cs.pc, inst[0] & 0xFFF, inst[0], u);

			gpu->cs.pc += 4;
		}
		break;

	/* More Unknown */

	case 0x3A1:
		/* 3A1	Set Lo Addresses; always comes in a pair with 5A1
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 *	llll llll llll llll llll llll llll llll
		 *	LLLL LLLL LLLL LLLL LLLL LLLL LLLL LLLL
		 *      0000 0000 0000 0000 0000 0000 0000 0000
		 *
		 * See PH:@0C016308
		 */
		{
			VK_LOG ("GPU CMD %08X: Set Lo Addresses [%08X %08X %08X %08X]",
			        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3]);
			ASSERT (!inst[3]);
			gpu->cs.pc += 16;
		}
		break;
	case 0x5A1:
		/* 5A1	Set Hi Addresses; always comes in a pair with 3A1
		 *
		 *	---- ---- ---- ---- ---- oooo oooo oooo
		 *	uuuu uuuu uuuu uuuu uuuu uuuu uuuu uuuu
		 *	UUUU UUUU UUUU UUUU UUUU UUUU UUUU UUUU
		 *      0000 0000 0000 0000 0000 0000 0000 0000
		 *
		 * See PH:@0C016308
		 */
		{
			VK_LOG ("GPU CMD %08X: Set Hi Addresses [%08X %08X %08X %08X]",
			        gpu->cs.pc, inst[0], inst[1], inst[2], inst[3]);
			ASSERT (!inst[3]);
			gpu->cs.pc += 16;
		}
		break;
	case 0x6D1:
		/* 6D1	Unknown
		 *
		 *	---- ---- ---- --nn ---- oooo oooo oooo
		 *	bbbb bbbb bbbb bbbb cccc cccc cccc cccc
		 *
		 * These come in quartets. May be related to matrices.
		 * See PH:@0C015C3E
		 */
		{
			unsigned a = inst[0] >> 16;
			unsigned b = inst[1] & 0xFFFF;
			unsigned c = inst[1] >> 16;
			VK_LOG ("GPU CMD %08X: Unknown: Set 6D1 [%08X %08X] <%u %u %u>",
			        gpu->cs.pc, inst[0], inst[1], a, b, c);
			gpu->cs.pc += 8;
		}
		break;
	case 0x181:
		/* 181	Unknown
		 *
		 *	---- ---b nnnn nnnn ---- oooo oooo oooo
		 *
		 * n = Unknown
		 * b = set only if n is non-zero
		 *
		 * See PH:@0C015B50
		 */
		{
			unsigned b = (inst[0] >> 24) & 1;
			unsigned n = (inst[0] >> 16) & 0xFF;
			VK_LOG ("GPU CMD %08X: Unknown: set 181 [%08X] <%u %u>",
			        gpu->cs.pc, inst[0], b, n);
			gpu->cs.pc += 4;
		}
		break;
	case 0xE88:
		/* E88	Unknown [Flush Vertices?] */
		{
			VK_LOG ("GPU CMD %08X: Unknown %03X [%08X]",
			        gpu->cs.pc, inst[0] & 0xFFF, inst[0]);
			gpu->cs.pc += 4;
		}
		break;

	/* For additional instructions, see:
	 *  - Instruction 02A: PH:@0C0DECC0
	 *  - Instruction 711: PH:@0C0162E2
	 */

	default:
		VK_ERROR ("GPU: @%08X: unhandled opcode %03X", gpu->cs.pc, inst[0] & 0xFFF);
		exit (1);
	}

	if (!is_vertex_op (op))
		hikaru_renderer_end_vertex_data (gpu->base.mach->renderer);

	return 0;
}
