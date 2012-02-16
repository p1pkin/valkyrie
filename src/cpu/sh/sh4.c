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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "vk/core.h"
#include "vk/cpu.h"

#include "sh4.h"
#include "sh4-ireg.h"

#define _RN	((inst >> 8) & 15)
#define _RM	((inst >> 4) & 15)
#define _UIMM8	((uint32_t)(uint8_t) inst)
#define _SIMM8	((int32_t)(int8_t) inst)
#define _SIMM12 ((int32_t)((inst & 0xFFF) | ((inst & 0x800) ? 0xFFFFF000 : 0)))

#define PC	ctx->pc
#define PR	ctx->pr
#define SR	ctx->sr
#define MAC	ctx->mac.full
#define MACH	ctx->mac.field.hi
#define MACL	ctx->mac.field.lo
#define GBR	ctx->gbr
#define VBR	ctx->vbr
#define SSR	ctx->ssr
#define SPC	ctx->spc
#define DBR	ctx->dbr
#define SGR	ctx->sgr

#define R(n_)	ctx->r[n_]
#define R0	R(0)
#define RN	R(_RN)
#define RM	R(_RM)
#define SP	R(15)

#define RBANK(n_) ctx->rbank[n_]

#define T	SR.bit.t
#define S	SR.bit.s
#define Q	SR.bit.q
#define M	SR.bit.m

#define FPSCR	ctx->fpscr
#define FPUL	ctx->fpul

#define FR(n_)	ctx->f.f[n_]
#define DR(n_)	ctx->f.d[(n_)/2]
#define FV(n_)	{ \
			ctx->f.f[(n_) / 4 + 0], \
			ctx->f.f[(n_) / 4 + 1], \
			ctx->f.f[(n_) / 4 + 2], \
			ctx->f.f[(n_) / 4 + 3], \
		}

#define XF(n_)	ctx->x.f[n_]
#define XD(n_)	ctx->x.d[(n_)/2]

#define FRN	FR(_RN)
#define FRM	FR(_RM)
#define FR0	FR(0)

#define DRN	DR(_RN)
#define DRM	DR(_RM)

#define XDN	XD(_RN)
#define XDM	XD(_RM)

#define IS_PRIVILEGED \
	(SR.bit.md == 1)

#define IS_FP_ENABLED \
	(SR.bit.fd == 0)

/* TODO trigger an illegal instruction exception */
#define CHECK_PM \
	do { \
		VK_ASSERT (IS_PRIVILEGED); \
	} while (0);

#define CHECK_FP \
	do { \
		VK_ASSERT (IS_FP_ENABLED); \
	} while (0);


/* Taken from MAME */
#define SHRINK(addr_) \
	((((addr_) >> 8) & 0xFF00) | ((addr_) & 0xFF))

#define IREG_GET(size_, addr_) \
	vk_buffer_get (ctx->iregs, size_, SHRINK (addr_))

#define IREG_PUT(size_, addr_, val_) \
	vk_buffer_put (ctx->iregs, size_, SHRINK (addr_), val_)

/* Forward declarations */
static int	sh4_get (sh4_t *, unsigned, uint32_t, void *);
static int	sh4_put (sh4_t *, unsigned, uint32_t, uint64_t);
static void	sh4_update_irqs (sh4_t *ctx);
static void	delay_slot (sh4_t *ctx, uint32_t pc);

static void
swap_r_banks (sh4_t *ctx)
{
	unsigned i;
	for (i = 0; i < 8; i++) {
		uint32_t tmp = R(i);
		R(i) = RBANK(i);
		RBANK(i) = tmp;
	}
}

static void
swap_f_banks (sh4_t *ctx)
{
	unsigned i;
	for (i = 0; i < 16; i++) {
		uint32_t tmp = FR(i).u;
		FR(i).u = XF(i).u;
		XF(i).u = tmp;
	}
}

static unsigned
get_r_bank_num (sh4_sr_t sr)
{
	/* In user mode, bank 0 is always selected; in privileged mode, bank
	 * SR.bit.rb is selected; it follows that bank 1 is only selected if
	 * both MD and RB are set. */
	return sr.bit.md & sr.bit.rb;
}

static void
set_sr (sh4_t *ctx, uint32_t data)
{
	sh4_sr_t old = SR;

	SR.full = data & 0x700083F3;

	/* Swap R banks if required */
	if (get_r_bank_num (old) != get_r_bank_num (SR))
		swap_r_banks (ctx);

	/* If any IRQ-related bits changed, re-validate pending IRQs */
	if ((old.bit.i != SR.bit.i) ||
	    (old.bit.bl != SR.bit.bl))
		sh4_update_irqs (ctx);
}

static uint32_t
get_sr (sh4_t *ctx)
{
	return SR.full;
}

/* TODO respect RM setting */

static void
set_fpscr (sh4_t *ctx, uint32_t val)
{
	sh4_fpscr_t old = FPSCR;

	FPSCR.full = val & 0x003FFFFF;
	/* Swap FR banks if required */
	if (old.bit.fr != FPSCR.bit.fr)
		swap_f_banks (ctx);

	/* SZ and PR can't be both set */
	if (FPSCR.bit.sz && FPSCR.bit.pr)
		VK_CPU_ABORT (ctx, "invalid FPSCR: SZ and PR bot set");
}

static uint32_t
get_fpscr (sh4_t *ctx)
{
	return FPSCR.full;
}

/* XXX Port A emulation is still very rough; we probably want to notify the
 * external handlers about the directions of all bits; although they already
 * probably know what to do with them. */

static void
set_porta (sh4_t *ctx, uint16_t data)
{
	uint32_t pctra;
	uint16_t pdtra;
	unsigned i;

	VK_ASSERT (ctx->porta_put);

	pdtra = IREG_GET (2, BSC_PDTRA);
	pctra = IREG_GET (4, BSC_PCTRA);

	for (i = 0; i < 16; i++) {
		unsigned direction = (pctra >> (i * 2)) & 1;
		/* only override those bits that are set to output */
		if (direction != 0) {
			pdtra &= ~(1 << i);
			pdtra |= data & (1 << i);
		}
	}
	if (ctx->porta_put (ctx, pdtra))
		VK_ASSERT (0);
}

static uint16_t
get_porta (sh4_t *ctx)
{
	uint32_t pctra;
	uint16_t pdtra, data;
	unsigned i;

	VK_ASSERT (ctx->porta_get);

	pdtra = IREG_GET (2, BSC_PDTRA);
	pctra = IREG_GET (4, BSC_PCTRA);

	if (ctx->porta_get (ctx, &data))
		VK_ASSERT (0);
	for (i = 0; i < 16; i++) {
		unsigned direction = (pctra >> (i * 2)) & 1;
		/* only override those bits that are set to input */
		if (direction == 0) {
			pdtra &= ~(1 << i);
			pdtra |= data & (1 << i);
		}
	}
	return pdtra;
}

/* On-chip Modules */

static int
sh4_ireg_get (sh4_t *ctx, unsigned size, uint32_t addr, void *val)
{
	VK_CPU_LOG (ctx, "IREG R%u %08X", size * 8, addr);

	set_ptr (val, size, IREG_GET (size, addr));

	/* See Table A.1, "Address List" */
	switch (addr & 0xFFFFFF) {
	case BSC_RFCR:
	case CPG_WTCSR:
	case INTC_IPRA:
	case INTC_IPRB:
	case INTC_IPRC:
		VK_ASSERT (size == 2);
		break;
	case BSC_PDTRA:
		VK_ASSERT (size == 2);
		set_ptr (val, size, get_porta (ctx));
		break;
	case CCN_CCR:
	case CCN_INTEVT:
	case BSC_PCTRA:
	case DMAC_DMAOR:
		VK_ASSERT (size == 4);
		break;
	default:
		return -1;
	}
	return 0;
}

static int
sh4_ireg_put (sh4_t *ctx, unsigned size, uint32_t addr, uint64_t val)
{
	VK_CPU_LOG (ctx, "IREG W%u %08X = %llX", size * 8, addr, val);

	/* See Table A.1, "Address List" */
	switch (addr & 0xFFFFFF) {
	case 0x900000 ... 0x90FFFF: /* BSC_SDRM2 */
	case 0x940000 ... 0x94FFFF: /* BSC_SDRM3 */
	case CPG_STBCR:
	case TMU_TOCR:
		VK_ASSERT (size == 1);
		break;
	case BSC_BCR2:
	case BSC_PCR:
	case BSC_RTCSR:
	case BSC_RTCNT:
	case BSC_RTCOR:
	case BSC_RFCR:
	case CPG_WTCSR:
	case INTC_ICR:
	case INTC_IPRA:
	case INTC_IPRB:
	case INTC_IPRC:
	case TMU_TCR0:
	case TMU_TCR1:
	case TMU_TCR2:
		VK_ASSERT (size == 2);
		break;
	case BSC_PDTRA:
		VK_ASSERT (size == 2);
		set_porta (ctx, val);
		break;
	case CCN_MMUCR:
	case CCN_CCR:
	case CCN_QACR0:
	case CCN_QACR1:
	case BSC_BCR1:
	case BSC_WCR1:
	case BSC_WCR2:
	case BSC_WCR3:
	case BSC_MCR:
	case BSC_PCTRA:
	case DMAC_TCR0:
	case DMAC_TCR1:
	case DMAC_TCR2:
	case DMAC_TCR3:
	case DMAC_CHCR0:
	case DMAC_CHCR1:
	case DMAC_CHCR2:
	case DMAC_CHCR3:
	case DMAC_DMAOR:
		VK_ASSERT (size == 4);
		break;
	default:
		return -1;
	}
	IREG_PUT (size, addr, val);
	return 0;
}

/* Store Queues */

static uint32_t
get_sq_addr (sh4_t *ctx, uint32_t addr)
{
	uint32_t sq_num = (addr >> 5) & 1;
	uint32_t sq_base = (sq_num == 0) ?
	                   vk_buffer_get (ctx->iregs, 4, SHRINK (CCN_QACR0)) :
	                   vk_buffer_get (ctx->iregs, 4, SHRINK (CCN_QACR1));
	return ((sq_base & 0x1C) << 24) | (addr & 0x03FFFFE0);
}

static int
sh4_sq_get (sh4_t *ctx, unsigned size, uint32_t addr, void *val)
{
	uint32_t sq_addr = get_sq_addr (ctx, addr);
	return sh4_get (ctx, size, sq_addr | (addr & 0x1F), val);
}

static int
sh4_sq_put (sh4_t *ctx, unsigned size, uint32_t addr, uint64_t val)
{
	uint32_t sq_addr = get_sq_addr (ctx, addr) | (addr & 0x1F);
	VK_CPU_LOG (ctx, "SQ W%u %08X = %llX", size * 8, addr, val);
	return sh4_put (ctx, size, sq_addr, val);
}

/* Bus Access */

/* TODO MMU, cache */

static int
sh4_fetch (sh4_t *ctx, uint32_t addr, uint16_t *inst)
{
	int ret;

	ret = vk_cpu_get ((vk_cpu_t *) ctx, 2, addr & ADDR_MASK, (void *) inst);
	if (ret)
		VK_CPU_ABORT (ctx, "unhandled fetch @%08X", addr);
	return 0;
}

static int
sh4_get (sh4_t *ctx, unsigned size, uint32_t addr, void *val)
{
	int ret;

	if (IS_ON_CHIP (addr))
		ret = sh4_ireg_get (ctx, size, addr, val);
	else if (IS_STORE_QUEUE (addr))
		ret = sh4_sq_get (ctx, size, addr, val);
	else
		ret = vk_cpu_get ((vk_cpu_t *) ctx, size, addr & ADDR_MASK, val);
	/* TODO propagate to the caller to allow for memory exceptions */
	if (ret)
		VK_CPU_ABORT (ctx, "unhandled R%d @%08X", 8*size, addr);
	return ret;
}

static int
sh4_put (sh4_t *ctx, unsigned size, uint32_t addr, uint64_t val)
{
	int ret;

	if (IS_ON_CHIP (addr))
		ret = sh4_ireg_put (ctx, size, addr, val);
	else if (IS_STORE_QUEUE (addr))
		ret = sh4_sq_put (ctx, size, addr, val);
	else
		ret = vk_cpu_put ((vk_cpu_t *) ctx, size, addr & ADDR_MASK, val);
	/* TODO propagate to the caller to allow for memory exceptions */
	if (ret)
		VK_CPU_ABORT (ctx, "unhandled W%d @%08X = %llX", 8*size, addr, val);
	return ret;
}

static inline uint8_t
R8 (sh4_t *ctx, uint32_t addr)
{
	uint8_t tmp;
	sh4_get (ctx, 1, addr, &tmp);
	return tmp;
}

static inline uint16_t
R16 (sh4_t *ctx, uint32_t addr)
{
	uint16_t tmp;
	sh4_get (ctx, 2, addr, &tmp);
	return tmp;
}

static inline uint32_t
R32 (sh4_t *ctx, uint32_t addr)
{
	uint32_t tmp;
	sh4_get (ctx, 4, addr, &tmp);
	return tmp;
}

static inline uint64_t
R64 (sh4_t *ctx, uint32_t addr)
{
	uint64_t tmp;
	sh4_get (ctx, 8, addr, &tmp);
	return tmp;
}

static inline void
W8 (sh4_t *ctx, uint32_t addr, uint8_t val)
{
	sh4_put (ctx, 1, addr, val);
}

static inline void
W16 (sh4_t *ctx, uint32_t addr, uint16_t val)
{
	sh4_put (ctx, 2, addr, val);
}

static inline void
W32 (sh4_t *ctx, uint32_t addr, uint32_t val)
{
	sh4_put (ctx, 4, addr, val);
}

static inline void
W64 (sh4_t *ctx, uint32_t addr, uint64_t val)
{
	sh4_put (ctx, 8, addr, val);
}

/* Interrupt Controller */

/* TODO add NMI support */
/* TODO add on-chip peripheral support */
/* TODO add exception support */

static void
sh4_update_irqs (sh4_t *ctx)
{
	uint16_t icr = IREG_GET (2, INTC_ICR);
	unsigned level;
	ctx->irq_pending = false;
	/* NMI is accepted even if BL is set when:
	 *  - the CPU is in SLEEP or STANDBY state
	 *  - ICR.NMIB is set
	 */
	if (ctx->irqs[16].state == VK_IRQ_STATE_RAISED) {
		ctx->irq_pending = (SR.bit.bl == 0) || ((icr & 0x0200) != 0);
		if (ctx->irq_pending)
			return;
	}
	if (SR.bit.bl)
		return;
	for (level = 16; level > SR.bit.i; level--) {
		if (ctx->irqs[level].state == VK_IRQ_STATE_RAISED) {
			ctx->irq_pending = true;
			break;
		}
	}
}

static void
sh4_set_irq_state (vk_cpu_t *cpu, vk_irq_state_t state, unsigned level, uint32_t vector)
{
	sh4_t *ctx = (sh4_t *) cpu;

	assert (state < VK_NUM_IRQ_STATES);
	assert (level <= 16);
	assert (vector);

	if (state == VK_IRQ_STATE_RAISED &&
	    ctx->irqs[level].state == VK_IRQ_STATE_RAISED &&
	    ctx->irqs[level].vector != vector) {
		VK_CPU_LOG (ctx, "overriding IRQ %d with new vector %08X",
		            level, vector);
	}

	ctx->irqs[level].state = state;
	ctx->irqs[level].vector = vector;

	if (level == 16) {
		/* NMI, set ICR bit 15 */
		uint16_t icr = IREG_GET (2, INTC_ICR) | 0x8000;
		IREG_PUT (2, INTC_ICR, icr);
	}

	sh4_update_irqs (ctx);
}

void
sh4_process_irqs (vk_cpu_t *cpu)
{
	sh4_t *ctx = (sh4_t *) cpu;
	unsigned level;

	/* Nothing to process if no IRQs or if BL is set */
	if (!ctx->irq_pending)
		return;

	/* Loop over all possible IRQs, highest priority first */
	for (level = 16; level > SR.bit.i; level--) {
		if (ctx->irqs[level].state == VK_IRQ_STATE_RAISED) {
			uint32_t vector, icr = IREG_GET (4, INTC_ICR);
			sh4_sr_t tmp;

			SPC = PC;
			SSR = SR;
			SGR = R(15);

			/* ICR.IRLM set means that IRL pins are treated
			 * as four indepentent IRQ lines, with a fixed
			 * vector. */
			if (icr & 0x80) {
				vector = 0x600;
				PC = VBR + 0x600;
			} else {
				VK_CPU_ABORT (cpu, "non-IRL IRQ unhandled: icr = %04X", icr);
			}

			VK_CPU_LOG (ctx, "IRQ taken: mask=%d level=%d vector=%04X:%08X PC=%08X",
			            SR.bit.i, level, vector, VBR + vector, PC);

			tmp.full    = SR.full;
			tmp.bit.bl = 1;
			tmp.bit.md = 1;
			tmp.bit.rb = 1;
			set_sr (ctx, tmp.full);

			IREG_PUT (4, CCN_INTEVT, ctx->irqs[level].vector);

			ctx->irqs[level].state  = false;
			ctx->irqs[level].vector = 0;

			sh4_update_irqs (ctx);

			break;
		}
	}
}

/* Instructions */

/* We need to define: itype, I, IDEF, IS_SH4, CHECK_PM, CHECK_FP */

typedef void (* itype) (sh4_t *ctx, uint16_t inst);

#define I(name_) \
	static void sh4_interp_##name_ (sh4_t *ctx, uint16_t inst)

#define IDEF(mask_, match_, name_) \
	{ \
		mask_, \
		match_, \
		sh4_interp_##name_, \
	}

#define IS_SH4

#include "sh-insns-interp.h"
#include "sh-insns-desc.h"

#undef I
#undef IDEF
#undef IS_SH4

#define CHECK_COLLISION \
	do { \
		if (insns[inst] != sh4_interp_invalid) { \
			VK_LOG ("inst=%04X insns[inst]=%p", inst, insns[inst]); \
			VK_ABORT ("insns table collision"); \
		} \
	} while (0);

static void
setup_insns_handlers_from_table (const idesctype *desc,
                                 unsigned size)
{
	unsigned i, j;
	uint16_t inst;

	for (i = 0; i < size; i++) {
		switch (desc[i].mask) {
		case 0xF000:
			for (j = 0; j < 4096; j++) {
				inst = desc[i].match | j;
				CHECK_COLLISION;
				insns[inst] = desc[i].handler;
			}
			break;
		case 0xF00F:
			for (j = 0; j < 256; j++) {
				inst = desc[i].match | (j << 4);
				CHECK_COLLISION;
				insns[inst] = desc[i].handler;
			}
			break;
		case 0xFF00:
			for (j = 0; j < 256; j++) {
				inst = desc[i].match | j;
				CHECK_COLLISION;
				insns[inst] = desc[i].handler;
			}
			break;
		case 0xF08F:
			for (j = 0; j < 128; j++) {
				inst = desc[i].match |
				       ((j & 7) << 4) |
				       ((j >> 3) << 8);
				CHECK_COLLISION;
				insns[inst] = desc[i].handler;
			}
			break;
		case 0xF0FF:
			for (j = 0; j < 16; j++) {
				inst = desc[i].match | (j << 8);
				CHECK_COLLISION;
				insns[inst] = desc[i].handler;
			}
			break;
		case 0xF1FF:
			for (j = 0; j < 8; j++) {
				inst = desc[i].match | (j << 9);
				CHECK_COLLISION;
				insns[inst] = desc[i].handler;
			}
			break;
		case 0xF3FF:
			for (j = 0; j < 4; j++) {
				inst = desc[i].match | (j << 10);
				CHECK_COLLISION;
				insns[inst] = desc[i].handler;
			}
			break;
		case 0xFFFF:
			inst = desc[i].match;
			CHECK_COLLISION;
			insns[inst] = desc[i].handler;
			break;
		default:
			VK_ABORT ("unhandled mask %04X", desc[i].mask);
		}
	}
}

#undef CHECK_COLLISION

static void
setup_insns_handlers (void)
{
	unsigned i;

	for (i = 0; i < 65536; i++) {
		insns[i] = sh4_interp_invalid;
	}

	setup_insns_handlers_from_table (insns_desc_sh2, NUMELEM (insns_desc_sh2));
	setup_insns_handlers_from_table (insns_desc_sh4, NUMELEM (insns_desc_sh4));
}

/* Execution */

static void
tick (sh4_t *ctx)
{
	//sh4_bsc_tick (ctx);
	//sh4_tmu_tick (ctx);
	//sh4_dmac_tick (ctx);
}

static void
sh4_step (sh4_t *ctx, uint32_t pc)
{
	uint16_t inst;
	uint32_t ppc = pc & 0x1FFFFFFF;

	sh4_fetch (ctx, pc, &inst);

	switch (ppc) {

	/* BOOTROM 0.92 */
	case 0x0C0010A4:
		VK_CPU_LOG (ctx, " ### IRQ: about to jump to hanlder @%08X", R(5))
		break;
	case 0x0C00B938:
		VK_CPU_LOG (ctx, " ### get_SAMURAI_params (%X, %X)", R(4), R(8));
		break;
	case 0x0C00BF30:
		VK_CPU_LOG (ctx, " ### check_SAMURAI_tag (%X, %X)", R(4), R(8));
		break;
	case 0x0C00BF52:
		VK_CPU_LOG (ctx, " ### check_SAMURAI_tag (%X, %X) : %X", R(4), R(8), R(1));
		break;
	case 0x0C00BC5C:
		VK_CPU_LOG (ctx, " ### authenticate_rom (%X, %X %X)", R(6), R(7), R(8));
		break;
	case 0x0C00BC9E:
		VK_CPU_LOG (ctx, " ### authenticate_rom () : values read from EPROM %X: %X %X",
		            R(5), R(3), R(1));
		break;
	case 0x0C004E46:
		VK_CPU_LOG (ctx, " ### rombd_do_crc (%X, %X, %X)", R(4), R(5), R(6));
		break;
	case 0x0C00660A:
		VK_CPU_LOG (ctx, " ### set_bank (%X %X)", R(4), R(5));
		break;
	case 0x0C006654:
		VK_CPU_LOG (ctx, " ### check_memory_range_1 (addr=%X, len32=%X)", R(4), R(5));
		break;
	case 0x0C0066EC:
		VK_CPU_LOG (ctx, " ### check_memory_range_2 (addr=%X, len32=%X)", R(4), R(5));
		break;
	case 0x0C00B90A:
		VK_CPU_LOG (ctx, " ### JUMPING TO ROM CODE! (%X)", R(11));
		break;
#if 0
	case 0x0C00204E:
		/* Allows slave to bypass check for DMAC IRQ of unknown source */
		T = 0;
		break;
#endif

	/* All games */
	case 0x0C00BFB2:
		/* XXX makes the EEPROM check pass */
		R(0) = 0xF;
		break;

	/* AIRTRIX */
	case 0x0C69B360:
		/* XXX skip MIE check */
		R(0) = R(1) = 0;
		break;
	case 0x0C03116E:
		VK_CPU_LOG (ctx, " ### AIRTRIX: unknown_crasher (%X, %X, %X)", R(4), R(5), R(6));
		break;
	}

	insns[inst] (ctx, inst);
	tick (ctx);
	ctx->base.remaining --;
}

static void
delay_slot (sh4_t *ctx, uint32_t pc)
{
	ctx->in_slot = true;
	sh4_step (ctx, pc);
	ctx->in_slot = false;
}

static int
sh4_run (vk_cpu_t *cpu, int cycles)
{
	sh4_t *ctx = (sh4_t *) cpu;

	cpu->remaining = cycles;
	while (cpu->remaining > 0) {
		if (cpu->state != VK_CPU_STATE_RUN)
			return 0;
		sh4_process_irqs (cpu);
		sh4_step (ctx, PC);
		PC += 2;
	}
	return -cpu->remaining;
}

void
sh4_set_state (vk_cpu_t *cpu, vk_cpu_state_t state)
{
	/* TODO: standby, deep sleep, etc. */
	cpu->state = state;
}

static void
sh4_reset (vk_cpu_t *cpu, vk_reset_type_t type)
{
	sh4_t *ctx = (sh4_t *) cpu;

	cpu->state = (ctx->config.master) ?
	              VK_CPU_STATE_RUN :
	              VK_CPU_STATE_STOP;

	memset (ctx->r, 0, sizeof (ctx->r));
	memset (ctx->f.f, 0, sizeof (ctx->f.f));
	memset (ctx->x.f, 0, sizeof (ctx->x.f));
	memset (ctx->rbank, 0, sizeof (ctx->rbank));
	memset (ctx->irqs, 0, sizeof (ctx->irqs));

	vk_buffer_clear (ctx->iregs);

	PC = 0xA0000000;
	PR = 0;
	SPC = 0;
	SGR = 0;
	VBR = 0;
	GBR = 0;
	DBR = 0;
	MAC = 0;

	SR.full = 0;
	SR.bit.i = 0xF;
	SR.bit.bl = 1;
	SR.bit.rb = 1;
	SR.bit.md = 1;

	SSR.full = 0;

	FPSCR.full = 0;
	FPSCR.bit.rm = 1;
	FPSCR.bit.dn = 1;
	FPUL.u    = 0;

	/* See Table A.1, "Address List" */
	IREG_PUT (2, BSC_BCR2, 0x3FFC);
	IREG_PUT (4, BSC_WCR1, 0x77777777);
	IREG_PUT (4, BSC_WCR2, 0xFFFEEFFF);
	IREG_PUT (4, BSC_WCR3, 0x07777777);
	IREG_PUT (4, TMU_TCOR0, 0xFFFFFFFF);
	IREG_PUT (4, TMU_TCNT0, 0xFFFFFFFF);
	IREG_PUT (4, TMU_TCOR1, 0xFFFFFFFF);
	IREG_PUT (4, TMU_TCNT1, 0xFFFFFFFF);
	IREG_PUT (4, TMU_TCOR2, 0xFFFFFFFF);
	IREG_PUT (4, TMU_TCNT2, 0xFFFFFFFF);
	IREG_PUT (1, SCI_SCBRR1, 0xFF);
	IREG_PUT (1, SCI_SCTDR1, 0xFF);
	IREG_PUT (1, SCI_SCSSR1, 0x84);
	IREG_PUT (1, SCIF_SCBRR2, 0xFF);
	IREG_PUT (2, SCIF_SCFSR2, 0x0060);
	IREG_PUT (2, UDI_SDIR, 0xFFFF);

	/* TODO: master/slave in BSC */

	ctx->in_slot = false;
	ctx->irq_pending = false;
}

static const char *
sh4_get_debug_string (vk_cpu_t *cpu)
{
	sh4_t *ctx = (sh4_t *) cpu;
	static char str[256];
	sprintf (str, "%c @%08X @%08X %08X",
	         ctx->config.master ? 'M' : 'S', PC, PR, SR.full);
	return str;
}

void
sh4_set_porta_handlers (vk_cpu_t *cpu,
                        int (* get)(sh4_t *ctx, uint16_t *val),
                        int (* put)(sh4_t *ctx, uint16_t val))
{
	sh4_t *ctx = (sh4_t *) cpu;

	ctx->porta_get = get;
	ctx->porta_put = put;
}

static void
sh4_delete (vk_cpu_t **cpu_)
{
	if (cpu_) {
		sh4_t *ctx = (sh4_t *) *cpu_;
		vk_buffer_delete (&ctx->iregs);
		free (ctx);
		*cpu_ = NULL;
	}
}

vk_cpu_t *
sh4_new (vk_machine_t *mach, vk_mmap_t *mmap, bool master)
{
	sh4_t *ctx = ALLOC (sh4_t);
	if (!ctx)
		goto fail;

	ctx->base.mach = mach;
	ctx->base.mmap = mmap;

	ctx->base.set_state		= sh4_set_state;
	ctx->base.run			= sh4_run;
	ctx->base.reset			= sh4_reset;
	ctx->base.set_irq_state		= sh4_set_irq_state;
	ctx->base.get_debug_string	= sh4_get_debug_string;
	ctx->base.delete		= sh4_delete;

	ctx->config.master = master;

	ctx->iregs = vk_buffer_le32_new (0x10000, 0);
	if (!ctx->iregs)
		goto fail;

	setup_insns_handlers ();

	return (vk_cpu_t *) ctx;
fail:
	vk_cpu_delete ((vk_cpu_t **) &ctx);
	return NULL;
}
