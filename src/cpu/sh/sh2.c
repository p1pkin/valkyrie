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

#include "sh2.h"
#include "sh-common.h"
#include "sh2-ireg.h"

#define SH2_LOG_INSNS			(1 << 0)
#define SH2_LOG_IREG_ACCESS		(1 << 1)
#define SH2_LOG_UNALIGNED_ACCESS	(1 << 2)
#define SH2_LOG_IRQS			(1 << 3)
#define SH2_LOG_JUMPS			(1 << 4)
#define SH2_LOG_DIVU			(1 << 5)

/*
 * TODO:
 *  - BSC counter
 *  - WDT
 *  - FRT counter
 *  - SPI
 *  - Proper instruction timing
 *  - Memory access timing
 *  - Cache emulation (16-byte data/inst prefetch, disable, purge, etc.)
 */

typedef union {
	struct {
		uint32_t t  : 1;
		uint32_t s  : 1;
		uint32_t    : 2;
		uint32_t i  : 4;
		uint32_t q  : 1;
		uint32_t m  : 1;
		uint32_t    : 22;
	} bit;
	uint32_t val;
} sh2_sr;

typedef struct {
	vk_cpu base;

	uint32_t	r[16];
	uint32_t	pc;
	sh2_sr		sr;
	uint32_t	pr;
	uint32_t	gbr;
	uint32_t	vbr;
	pair32u		mac;

	bool		master;
	bool		in_slot;
	bool		irq_pending;
	vk_irq		irqs[17];

	uint8_t		ireg[0x200];

	/* Free-Running Timer */
	struct {
		uint16_t frc;
		uint16_t ocra, ocrb;
		uint16_t icr;
	} frt;
} sh2;

#include "cpu/sh/sh2-ireg.h"

#define IREG(addr_)	ctx->ireg[(addr_) & 0x1FF]

#define IREG8(addr_)	IREG(addr_)
#define IREG16(addr_)	(*(uint16_t *) &IREG (addr_))
#define IREG32(addr_)	(*(uint32_t *) &IREG (addr_))

/*
 * Division Unit (DIVU)
 *
 * Overflow/underflow occurs when:
 * - the results of operations exceed the int32_t range, e.g.,
 *   - the result is a different sign than the operands
 *   - the divisor is 0
 * - if the IRQ is not configured, the result will be saturated
 * - there's still something wrong with the Saturn BIOS U intro polys, it may
 *   be not related to DIVU tho.
 */

#define DVSR	IREG32 (DIVU_DVSR)
#define DVDNT	IREG32 (DIVU_DVDNT)
#define DVCR	IREG32 (DIVU_DVCR)
#define DVDNTH	IREG32 (DIVU_DVDNTH)
#define DVDNTL	IREG32 (DIVU_DVDNTL)

static void
divu_perform_32_32_division (sh2 *ctx)
{
	int64_t p, q, d, r;
	bool oflow, uflow;

	q = (int32_t) DVSR;
	p = (int32_t) DVDNT;

	assert (q != 0);

	d = p / q;
	r = p % q;

	oflow = (d > (int64_t) 0x000000007FFFFFFFl); 
	uflow = (d < (int64_t) 0xFFFFFFFF80000000l);

	LOG (SH2_LOG_DIVU, "DIVU 32: %X / %X = ( %X, %X ) [o:%d u:%d]",
	     p, q, d, r, oflow, uflow);

	assert (!(oflow || uflow));
	assert (!(DVCR & 2) || !(oflow || uflow));

	DVDNTH = (uint32_t) r;
	DVDNTL = (uint32_t) d;
	DVDNT  = (uint32_t) d;

	/* No overflow occurred */
	DVCR &= ~1;
}

static void
divu_perform_64_32_division (sh2 *ctx)
{
	int64_t p, d, r, q;
	bool oflow, uflow;

	q = (int64_t) (int32_t) DVSR;
	p = (int64_t) ((((uint64_t) DVDNTH) << 32) | ((uint64_t) DVDNTL));

	if (q == 0) {
		oflow = (p >= 0);
		uflow = (p <  0);
	} else {
		d = p / q;
		r = p % q;
		oflow = (d > (int64_t) 0x000000007FFFFFFFl);
		uflow = (d < (int64_t) 0xFFFFFFFF80000000l);
	}

	if (oflow) {
		d = 0x000000007FFFFFFFl;
		r = 0x0000000000000000l;
	}
	if (uflow) {
		d = 0xFFFFFFFF80000000l;
		r = 0xFFFFFFFFFFFFFFFFl;
	}

	LOG (SH2_LOG_DIVU, "DIVU 64: %lX / %X = ( %X, %X ) [o:%d u:%d]",
	      p, q, d, r, oflow, uflow);

	DVDNTH = (uint32_t) r;
	DVDNTL = (uint32_t) d;
	DVDNT  = (uint32_t) d;

	if (oflow || uflow) {
		assert (!(DVCR & 2));
		DVCR |= 1;
	} else {
		DVCR &= ~1;
	}
}

/******************************************************************************/
/* DMA Controller                                                             */
/******************************************************************************/

/*
 * TODO:
 *  - DMAC priority
 *  - DMAC 16-byte transfer
 */

typedef struct {
	uint32_t dme  : 1; /* DMA enabled */
	uint32_t nmif : 1; 
	uint32_t ae   : 1;
	uint32_t pr   : 1;
	uint32_t      : 28;
} dmac_dmaor;

typedef struct {
	uint32_t de : 1; /* DMA enabled */
	uint32_t te : 1; /* DMA ended */
	uint32_t ie : 1; /* IRQ enabled */
	uint32_t ta : 1; /* */
	uint32_t tb : 1; /* */
	uint32_t dl : 1; /* */
	uint32_t ds : 1; /* */
	uint32_t al : 1; /* dack mode */
	uint32_t am : 1; /* dack output mode */
	uint32_t ar : 1; /* request mode (auto or module) */
	uint32_t ts : 2; /* transfer size */
	uint32_t sm : 2; /* source address mode */
	uint32_t dm : 2; /* destination address mode */
} dmac_chcr;

#define DRCR(n)		IREG8 (DMAC_DRCR0 + (n))
#define SAR(n)		IREG32 (DMAC_SAR0 + (((n) == 0) ? 0 : 0x10))
#define DAR(n)		IREG32 (DMAC_DAR0 + (((n) == 0) ? 0 : 0x10))
#define TCR(n)		IREG32 (DMAC_TCR0 + (((n) == 0) ? 0 : 0x10)) /* 24 bit, 0 = max */
#define CHCR(n)		((dmac_chcr *) &IREG32 (DMAC_CHCR0 + (((n) == 0) ? 0 : 0x10)))
#define DMAOR		((dmac_dmaor *) &IREG32 (DMAC_DMAOR))

static const int dmac_incr[4] = { 0, 1, -1, 0x80000000 };

static void
dmac_print (sh2 *ctx, unsigned ch)
{
	sh_println (stdout, ctx, " DMAC%d: %08X -> %08X (%06X) [d:%d s:%d #:%d d/s:%d irq:%d dmaor:%02X drcr:%02X]\n",
	             ch, SAR (ch), DAR (ch), TCR (ch),
	             CHCR(ch)->dm, CHCR(ch)->sm, CHCR(ch)->ts, CHCR(ch)->ta, CHCR(ch)->ie,
	             IREG32 (DMAC_DMAOR), DRCR (ch));
}

static void
dmac_tick_channel (sh2 *ctx, unsigned ch)
{
	if (DMAOR->dme && CHCR (ch)->de && !CHCR (ch)->te) {

		unsigned tmp;

		if (DMAOR->nmif || DMAOR->ae) {
			return;
		}

		assert (CHCR (ch)->ar == 1); /* auto request, not SCI nor external pin */
		assert (CHCR (ch)->ta == 0); /* double address mode */
		assert (CHCR (ch)->sm != 3);
		assert (CHCR (ch)->dm != 3);

		/* FIXME: can't access the DMAC, BSC, etc. from DMAC */

		switch (CHCR (ch)->ts) {
		case 0:
			tmp = sh2_read8 (ctx, SAR (ch));
			SAR (ch) += dmac_incr[CHCR (ch)->sm];
			sh2_write8 (ctx, DAR (ch), tmp);
			DAR (ch) += dmac_incr[CHCR (ch)->dm];
			break;
		case 1:
			tmp = sh2_read16 (ctx, SAR (ch));
			SAR (ch) += dmac_incr[CHCR (ch)->sm] * 2;
			sh2_write16 (ctx, DAR (ch), tmp);
			DAR (ch) += dmac_incr[CHCR (ch)->dm] * 2;
			break;
		case 2:
			tmp = sh2_read32 (ctx, SAR (ch));
			SAR (ch) += dmac_incr[CHCR (ch)->sm] * 4;
			sh2_write32 (ctx, DAR (ch), tmp);
			DAR (ch) += dmac_incr[CHCR (ch)->dm] * 4;
			break;
		default:
			assert (!(SAR (ch) & 15));
			assert (!(DAR (ch) & 15));
			assert (0);
			break;
		}

		TCR (ch) --;
		if (TCR (ch) == 0) {

			/* no DMA address error */
			DMAOR->ae = 0;

			CHCR (ch)->te = 1;
			assert (!CHCR (ch)->ie); /* IRQ */
		}
	}
}

static void
dmac_tick (sh2 *ctx)
{
	/* TODO: priorities, see DMAOR->pr, and burst/cycle-steal */
	dmac_tick_channel (ctx, 0);
	dmac_tick_channel (ctx, 1);
}

/******************************************************************************/
/* Free-running Timer                                                         */
/******************************************************************************/

typedef struct
{
	uint8_t olvlb : 1;
	uint8_t olvla : 1;
	uint8_t oeb   : 1; /* ? */
	uint8_t oea   : 1; /* ? */
	uint8_t ocrs  : 1;
	uint8_t       : 3;

} frt_tocr;

typedef struct
{
	uint8_t cks   : 2; /* timer clock select */
	uint8_t bufea : 1; /* ? */
	uint8_t bufeb : 1; /* ? */
	uint8_t       : 3;
	uint8_t iedga : 1; /* interrupt capture mode */

} frt_tcr;

typedef struct
{
	uint8_t       : 1;
	uint8_t ovie  : 1; /* overflow IRQ enabled */
	uint8_t ocibe : 1; /* match B IRQ enabled */
	uint8_t ociae : 1; /* match A IRQ enabled */
	uint8_t       : 3;
	uint8_t icie  : 1; /* IRQ enabled */

} frt_tier;

typedef struct
{
	uint8_t cclra : 1;
	uint8_t ovf   : 1;
	uint8_t ocfb  : 1;
	uint8_t ocfa  : 1;
	uint8_t       : 3;
	uint8_t icf   : 1;

} frt_ftcsr;

#define FRC	ctx->frt.frc
#define OCRA	ctx->frt.ocra
#define OCRB	ctx->frt.ocrb
#define ICR	ctx->icr
#define TIER	((frt_tier *) &IREG8 (FRT_TIER))
#define FTCSR	((frt_ftcsr *) &IREG8 (FRT_FTCSR))
#define TOCR	((frt_tocr *) &IREG8 (FRT_TOCR))
#define FTCR	((frt_tcr *) &IREG8 (FRT_TCR))

/* TODO: use a scheduler instead of polling... */
/* TODO: proper support for clock */

static void
frt_tick (sh2 *ctx)
{
	FRC ++;

	if (FRC == 0) {
		FTCSR->ovf = 1;
		assert (!TIER->ovie);
	}

	if (FRC == OCRA) {
		TOCR->olvla = 1;
		if (FTCSR->cclra) {
			FRC = 0;
		}
		assert (!TIER->ociae);
	}

	if (FRC == OCRB) {
		TOCR->olvlb = 1;
		assert (!TIER->ocibe);
	}
}

void
sh2_send_frt (sh2 *ctx)
{
	LOG (SH2_LOG_FRT, "FRT input captured");

	FTCSR->icf = 1;
	ICR = FRC;

	assert (!TIER->icie);
}

/*******************************************************************************
 On-Chip Register Access
*******************************************************************************/

static const ireg_desc iregs[0x200] = {
	[INTC_ICR & 0x1FF]	= { "INTC_ICR",		1|2,	NULL,	NULL },
	[INTC_IPRA & 0x1FF]	= { "INTC_IPRA",	1|2,	NULL,	NULL },
#define	INTC_ICR	0xFFFFFEE0
#define	INTC_IPRA	0xFFFFFEE2
#define	INTC_IPRB	0xFFFFFE60
#define	INTC_VCRA	0xFFFFFE62
#define	INTC_VCRB	0xFFFFFE64
#define	INTC_VCRC	0xFFFFFE66
#define	INTC_VCRD	0xFFFFFE68
#define	INTC_VCRWDT	0xFFFFFEE4
#define	INTC_VCRDIV	0xFFFFFF0C
#define INTC_VCRDIV2	0xFFFFFF2C
#define	INTC_VCRDMA0	0xFFFFFFA0
#define	INTC_VCRDMA1	0xFFFFFFA8
};

static uint32_t
sh2_ireg_read (sh2 *ctx, unsigned size, uint32_t addr)
{
	uint32_t data;
	switch (size) {
	case 1:
		data = IREG8 (addr);
		break;
	case 2:
		data = IREG16 (addr);
		break;
	case 4:
		data = IREG32 (addr);
		break;
	default:
		assert (0);
	}
	switch (addr) {
	case SCI_SCR:
	case SCI_SSR:
	}
}
	switch (addr) {

	/*
	 * SCI
	 */

	case SCI_SCR:
	case SCI_SSR:
		/* when this returns 0xFF, the sat u bios reads a program (?) and jumps to it */
		return 0;

	/*
	 * FRT (rsgun)
	 */

	case FRT_FTCSR:
		break;

	case FRT_FRCH:
		val = ctx->frc >> 8;
		break;

	case FRT_FRCL:
		val = ctx->frc & 0xFF;
		break;

	default:
		sh_println (stderr, ctx, "unhandled IREG R8: %08X\n", addr);
		exit (-1);
	}

#ifdef SH2_LOG_IREG_ACCESS
	sh_println (stdout, ctx, "IREG R8 %08X (%02X)\n", addr, val);
#endif

	return val;
}

static uint16_t
sh2_ireg_read16 (sh2 *ctx, uint32_t addr)
{
	uint16_t val;

	val = IREG16 (addr);

	switch (addr) {

	/*
	 * ICR
	 */

	case INTC_ICR:
		break;

	default:
		sh_println (stdout, ctx, "unhandled IREG R16 %08X\n", addr);
		exit (-1);
	}

#ifdef SH2_LOG_IREG_ACCESS
	sh_println (stdout, ctx, "IREG R16 %08X (%04X)\n", addr, val);
#endif

	return bswap16 (val);
}

static uint32_t
sh2_ireg_read32 (sh2 *ctx, uint32_t addr)
{
	uint32_t val;

	val = IREG32 (addr);

	switch (addr) {

	/*
	 * BSC
	 */

	case BSC_BCR1:
		break;

	/*
	 * INTC
	 */

	case INTC_VCRDIV2: // hanagumi
		val = IREG32 (INTC_VCRDIV);
		break;

	/*
	 * DIVU
	 */

	case DIVU_DVSR:
	case DIVU_DVSR2:
		val = DVSR;
		break;

	case DIVU_DVDNT:
	case DIVU_DVDNT2:
		val = DVDNT;
		break;

	case DIVU_DVDNTH:
	case DIVU_DVDNTH2:
	case DIVU_DVDNTH3:
	case DIVU_DVDNTH4:
		val = DVDNTH;
		break;

	case DIVU_DVDNTL:
	case DIVU_DVDNTL2:
	case DIVU_DVDNTL3:
	case DIVU_DVDNTL4:
		val = DVDNTL;
		break;

	/*
	 * DMAC
	 */

	case DMAC_DMAOR:
	case DMAC_CHCR0:
	case DMAC_CHCR1:
		break;

	default:
		sh_println (stderr, ctx, "unhandled IREG R32: %08X\n", addr);
		exit (-1);
	}

#ifdef SH2_LOG_IREG_ACCESS
	sh_println (stdout, ctx, "IREG R32 %08X (%08X)\n", addr, val);
#endif

	return bswap32 (val);
}

static void
sh2_ireg_write8 (sh2 *ctx, uint32_t addr, uint8_t val)
{
#ifdef SH2_LOG_IREG_ACCESS
	sh_println (stdout, ctx, "IREG W8 %08X = %02X\n", addr, val);
#endif

	switch (addr) {

	/*
	 * BSC
	 */

	case CCR_CCR:
		break;

	/*
	 * INTC
	 */

	case INTC_ICR:
	case INTC_ICR+1:
	case INTC_IPRA:
	case INTC_IPRA+1:
	case INTC_IPRB:
	case INTC_IPRB+1:
	case INTC_VCRA:
	case INTC_VCRA+1:
	case INTC_VCRB:
	case INTC_VCRB+1:
	case INTC_VCRC:
	case INTC_VCRC+1:
	case INTC_VCRD:
	case INTC_VCRD+1:
	case INTC_VCRWDT:
	case INTC_VCRWDT+1:
		break;

	/*
	 * DMAC
	 */

	case DMAC_DRCR0: /* STV 1.06, cart test */
	case DMAC_DRCR1: /* rsgun */
		assert (val == 0);
		break;

	/*
	 * FRT
	 */

	case FRT_TIER:
		IREG8 (addr) = (val & 0x8C) | 1;
		break;
	case FRT_FTCSR:
		IREG8 (addr) = (IREG8 (addr) & val & 0xFE) | (val & 1);
		return;
	case FRT_TCR:
		IREG8 (addr) = val & 0x83;
		return;

#if 0
	/* special handling: use a temporary 16-bit register */
	case FRT_FRCH:
	case FRT_FRCL:
	case FRT_OCRH:
	case FRT_OCRL:
#endif

	case FRT_ICRH:
	case FRT_ICRL:
		/* read-only */
		assert (0);
		break;

	/*
	 * SCI
	 */

	case SCI_SMR:
	case SCI_BRR:
	case SCI_SCR:
	case SCI_TDR:
	case SCI_SSR:
	case SCI_RDR:
		break;

	/*
	 * Sleep/Standby
	 */

	case SBYCR:
		//assert (!(val & 0x80)); /* 0=sleep==sleep 1=sleep=standby */
		assert (!(val & 0x40)); /* ping high/retained during standby */
		assert (!(val & 0x10)); /* DMAC standby */
		assert (!(val & 0x08)); /* MULTI standby */
		assert (!(val & 0x04)); /* DIVU standby */
		assert (!(val & 0x02)); /* FRT standby */
		assert (!(val & 0x01)); /* SCI standby */
		break;

	default:
		sh_println (stderr, ctx, "unhandled IREG W8: %08X = %02X\n", addr, val);
		exit (-1);
	}

	IREG8 (addr) = val;
}

static void
sh2_ireg_write16 (sh2 *ctx, uint32_t addr, uint16_t val)
{
	val = bswap16 (val);

#ifdef SH2_LOG_IREG_ACCESS
	sh_println (stdout, ctx, "IREG W16 %08X = %04X\n", addr, val);
#endif

	IREG16 (addr) = val;

	switch (addr) {

	/* CAS latency */
	case CAS_LATENCY_0:
	case CAS_LATENCY_1:
	case CAS_LATENCY_2:
		break;

	/* INTC */
	case INTC_ICR:
	case INTC_IPRA:
	case INTC_IPRB:
	case INTC_VCRA:
	case INTC_VCRB:
	case INTC_VCRC:
	case INTC_VCRD:
	case INTC_VCRWDT:
	case INTC_VCRDIV:
	case INTC_VCRDMA0:
	case INTC_VCRDMA1:
		break;

	case INTC_VCRDIV2:
		IREG16 (INTC_VCRDIV) = val;
		break;

	/* WDT */
	case WDT_WTCSR:
		assert (!(val & 0x20)); /* timer enable */
		/* val | 0x18 */
	case WDT_RSTCSR_W:
		break;

	default:
		sh_println (stderr, ctx, "unhandled IREG W16: %08X = %04X\n", addr, val);
		exit (-1);
	}
}

static const unsigned rtc_dividers[8] = {
	0,	4,	16,	64,
	256,	1024,	2048,	4096
};

static void
sh2_ireg_write32 (sh2 *ctx, uint32_t addr, uint32_t val)
{
	val = bswap32 (val);

#ifdef SH2_LOG_IREG_ACCESS
	sh_println (stdout, ctx, "IREG W32 %08X = %08X\n", addr, val);
#endif

	switch (addr) {

	/*
	 * BSC
	 */

	case BSC_BCR1:
	case BSC_BCR2:
	case BSC_WCR:
	case BSC_MCR:
	case BSC_RTCNT:
	case BSC_RTCOR:
		break;

	case BSC_RTCSR:
		assert (!(val & 0xC0));
		break;

	/*
	 * INTC
	 */

	case INTC_VCRDMA0:
	case INTC_VCRDMA1:
		break;

	case INTC_VCRDIV:
	case INTC_VCRDIV2:
		val = IREG32 (INTC_VCRDIV);
		return;

	/*
	 * DMAC
	 */

	case DMAC_SAR0:
	case DMAC_SAR1:
	case DMAC_DAR0:
	case DMAC_DAR1:
		break;

	case DMAC_TCR0:
	case DMAC_TCR1:
		/* TCRx is 24-bit */
		assert (!(val & 0xFF000000));
		break;

	case DMAC_CHCR0:
		if ((IREG32 (addr) & 1) == 0 && (val & 1) != 0) {
			dmac_print (ctx, 0);
		}
		/* should only be accessed with either DE=0 or DMAOR.DME=0 */
		/* lower 2 bits can only be cleared */
		IREG32 (addr) = (val & ~2) | (IREG32 (addr) & val & 2);
		return;

	case DMAC_CHCR1:
		if ((IREG32 (addr) & 1) == 0 && (val & 1) != 0) {
			dmac_print (ctx, 1);
		}
		/* should only be accessed with either DE=0 or DMAOR.DME=0 */
		/* lower 2 bits can only be cleared */
		IREG32 (addr) = (val & ~2) | (IREG32 (addr) & val & 2);
		return;

	case DMAC_DMAOR:
		/* bit 0,3 can be set, bits 1,2 can only be cleared */
		IREG32 (addr) = (val & 9) | (IREG32 (addr) & val & 6);
		return;

	/*
	 * DIVU
	 */

	/* TODO: writing to a DIVU reg while the division is in progress stalls
	 * the computation for one more cycle */

	case DIVU_DVSR:
	case DIVU_DVSR2:
		IREG32 (DIVU_DVSR) = val;
		return;

	case DIVU_DVDNT:
	case DIVU_DVDNT2:
		IREG32 (DIVU_DVDNT) = val;
		sh2_divu_perform_32_32_division (ctx);
		return;

	case DIVU_DVCR:
	case DIVU_DVCR2:
		IREG32 (DIVU_DVCR) = val;
		return;

	case DIVU_DVDNTH:
	case DIVU_DVDNTH2:
	case DIVU_DVDNTH3:
	case DIVU_DVDNTH4:
		IREG32 (DIVU_DVDNTH) = val;
		break;

	case DIVU_DVDNTL:
	case DIVU_DVDNTL2:
	case DIVU_DVDNTL3:
	case DIVU_DVDNTL4:
		IREG32 (DIVU_DVDNTL) = val;
		sh2_divu_perform_64_32_division (ctx);
		return;

	/*
	 * FRT
	 */

//	case FRT_TIER: // prikura
//	case FRT_ICRH: // prikura
//	case 0xFFFFFE24: // prikura, mirrors some FRT thing?
//	case 0xFFFFFE28: // prikura, mirrors some FRT thing?
		break;

	/*
	 * WDT
	 */

	case WDT_WTCSR: // prikura
		break;

	default:
		sh_println (stderr, ctx, "unhandled IREG W32: %08X = %08X\n", addr, val);
		exit (-1);
	}

	IREG32 (addr) = val;
}

#endif

/*******************************************************************************
 Memory Access
*******************************************************************************/

enum {
	AREA_CCS03  = 0x00, /* CS0-CS3, cached */
	AREA_UCS03  = 0x20, /* CS0-CS3, uncached */
	AREA_PURGE  = 0x40, /* Associative purge space */
	AREA_AARR   = 0x60, /* Address array */
	AREA_DARR   = 0xC0, /* Data array */
	AREA_ONCHIP = 0xF8, /* On-chip peripherals and DRAM settings */
};

#define AREA(a)		(((a) >> 24) & 0xF8)
#define ADDR_MASK	0x07FFFFFF

static uint16_t
sh2_fetch (sh2 *ctx, uint32_t addr)
{
	vk_cpu *cpu = vk_cpu (ctx);
	uint32_t area = AREA (addr);
	uint16_t inst;

	VK_MACH_ASSERT (cpu->mach, !!(addr & 1), "unaligned fetch @%08X\n", addr);

	/* We can only fetch from CS0-CS3, either cached or uncached.
	 * Everything else raises an address error. */
	if (area != AREA_CCS03 && area != AREA_UCS03) {
		VK_MACH_ABORT (cpu->mach, "bad fetch address");
	}

	/* TODO: prefetch */
	inst = (uint16_t) vk_cpu_read (cpu, 2, addr);
	if (area == AREA_CCS03) {
		/* TODO: cache */
	}
	return inst;
}

static const uint32_t addr_mask[3] = { 0, 1, 3 };
static const uint32_t data_mask[3] = { 0xFF, 0xFFFF, 0xFFFFFFFF };

#ifdef SH2_LOG
#define LOG VK_CPU_LOG
#else
#define LOG
#endif

#define ABORT VK_CPU_ABORT

uint32_t
sh2_read (sh2 *ctx, unsigned size, uint32_t addr)
{
	uint64_t data;

	assert (size <= 4);

	if (addr & addr_mask[size]) {
		VK_CPU_LOG (ctx, SH2_LOG_UNALIGNED_ACCESS,
		            "unaligned R%u @%08X\n", size, addr);
		/* TODO: special handling for endianess difference */
	}

	/* TODO: caching */
	switch (ADDR_AREA (addr)) {
	case AREA_CCS03:
		/* fallthrough */
	case AREA_UCS03:
		data = vk_cpu_read (vk_cpu (ctx), size, addr & ADDR_MASK);
		break;
	case AREA_ONCHIP:
		data = sh2_read_onchip (ctx, size, addr);
		break;
	default:
		ABORT (

		VK_MACH_ABORT (cpu->mach, "bad R%u address @%08X", size, addr);
		break;
	}

	return data & data_mask[size];
}

static void
sh2_write (vk_cpu *cpu, unsigned size, uint32_t addr, uint32_t data)
{
	assert (size <= 4);

	data &= data_mask[size];
	if (addr & addr_mask[size]) {
#ifdef SH2_LOG_UNALIGNED_ACCESS
		VK_MACH_LOG (cpu->mask, "unaligned W%u address @%08X", size, addr);
#endif
		/* TODO: special handling for endianess difference */
	}

	switch (ADDR_AREA (addr)) {
	case AREA_CCS03:
		/* TODO: cache */
		/* fallthrough */
	case AREA_UCS03:
		vk_cpu_write (cpu, size, addr & ADDR_MASK, val);
		break;
	case AREA_ONCHIP:
		sh2_write8_onchip (ctx, size, addr, val);
		break;
	default:
		VK_MACH_ABORT (cpu->mach, "bad R%u address @%08X", size, addr);
		break;
	}
}

#define R8(addr_)	((uint8_t) sh2_read (ctx, 
#define R16(addr_)	((uint16_t) sh2_read (ctx, 
#define R32(addr_)	((uint32_t) sh2_read (

#define W8(addr_)	
#define W16(addr_)	
#define W32(addr_)	

/*******************************************************************************
 Status Register
*******************************************************************************/

static void
sh2_set_sr (sh2 *ctx, uint32_t val)
{
	SR.val = val & 0x3F3;
	sh2_update_irqs (ctx);
}

static uint32_t
sh2_get_sr (sh2 *ctx)
{
	return SR.val;
}

/*******************************************************************************
 Interrupt Processing
*******************************************************************************/

#define INTC_DIV_PRIORITY	((IREG16 (INTC_IPRA) >> 12) & 15)
#define INTC_DMA_PRIORITY	((IREG16 (INTC_IPRA) >>  8) & 15)
#define INTC_WDT_PRIORITY	((IREG16 (INTC_IPRA) >>  4) & 15)
#define INTC_SCI_PRIORITY	((IREG16 (INTC_IPRB) >> 12) & 15)
#define INTC_FRT_PRIORITY	((IREG16 (INTC_IPRB) >>  8) & 15)

#define INTC_WDT_VECTOR		((IREG16 (INTC_VCRWDT) >> 8) & 127)	/* interval interrupt & watchdog interrupt */
#define INTC_BSC_VECTOR		(IREG16 (INTC_VCRWDT) & 127)		/* compare match interrupt */
#define INTC_SCI_ERI_VECTOR	((IREG16 (INTC_VCRA) >> 8) & 127)	/* receive-error int */
#define INTC_SCI_RXI_VECTOR	(IREG16 (INTC_VCRA) & 127)		/* receive-data-full int */
#define INTC_SCI_TXI_VECTOR	((IREG16 (INTC_VCRB) >> 8) & 127)	/* transmit-data-emtpy int */
#define INTC_SCI_TEI_VECTOR	(IREG16 (INTC_VCRB) & 127)		/* transmit-end int */
#define INTC_FRC_ICI_VECTOR	((IREG16 (INTC_VCRC) >> 8) & 127)	/* input-capture int */
#define INTC_FRC_OCI_VECTOR	(IREG16 (INTC_VCRC) & 127)		/* output-compare int */
#define INTC_FRC_OVI_VECTOR	((IREG16 (INTC_VCRD) >> 8) & 127)	/* overflow int */

/* TODO: update IRQs when on-chip priorities change */

static void
sh2_update_irqs (sh2 *ctx)
{
	int level;

	ctx->irq_pending = false;
	for (level = 16; level > SR.bit.i; level--) {
		if (ctx->irqs[level].state == VK_IRQ_STATE_RAISED) {
			ctx->irq_pending = true;
			break;
		}
	}
}

static void
sh2_set_irq_state (vk_cpu *cpu, vk_irq_state state, unsigned level, uint32_t vector)
{
	sh2 *ctx = vk_sh4 (cpu);

	assert (state < VK_NUM_IRQ_STATES);
	assert (level >= 0 && level < 17);
	assert (vector);

	if (state == VK_IRQ_STATE_RAISED &&
	    ctx->irqs[level].state == VK_IRQ_STATE_RAISED &&
	    ctx->irqs[level].vector != vector) {
		LOG ("overriding IRQ %d with new vector %08X\n", level, vector);
	}

	ctx->irqs[level].state = state;
	ctx->irqs[level].vector = vector;

	if (state == VK_IRQ_STATE_RAISED && level == 16) {
		/* NMI */
		IREG16 (INT_ICR) |= 0x8000;
		IREG32 (DMAC_DMAOR) |= 2;
		if ((IREG8 (SBYCR) & 0x80) &&
		    ctx->base.state == VK_CPU_STATE_STANDBY) {
			ctx->base.state = VK_CPU_STATE_RUN;
		}
	}

	sh2_update_irqs (ctx);
}

void
sh2_process_irqs (vk_cpu *cpu)
{
	sh2 *ctx = (sh2 *) cpu;
	int level;

	if (!ctx->irq_pending) {
		return;
	}

	for (level = 16; level > SR.bit.i; level--) {
		if (ctx->irqs[level].state == VK_IRQ_STATE_RAISED) {
			uint32_t vector = ctx->irqs[level].vector;
			uint32_t target = VBR + vector * 4;

			SP -= 4;
			W32 (SP, SR.val & 0x3F3);
			SP -= 4;
			W32 (SP, PC);
			PC = R32 (target);

			LOG (cpu, SH2_LOG_IRQS,
			     "IRQ taken: I=%d level=%d vector=%X (=%X) ---> %08X\n",
			     SR.bit.i, level, vector, VBR + vector * 4, PC);

			SR.bit.i = MIN (level, 15);

			/* XXX this is not exactly how it works... If the pins
			 * are still held, the IRQ request won't go away. That's
			 * what the above line is all about. */
			ctx->irqs[level].state  = VK_IRQ_STATE_CLEAR;
			ctx->irqs[level].vector = 0;

			sh2_update_irqs (ctx);

			/* Only the highest priority IRQ is handled */
			break;
		}
	}
}

#define IS_SH2
#define I(name) \
	static void sh2_interp_##name (sh2 *ctx, uint16_t inst)

#include "sh-insns-interp.h"

#undef IS_SH2
#undef I

static void
sh2_tick (sh2 *ctx)
{
	dmac_tick (ctx);
	frt_tick (ctx);
}

static void
sh2_step (sh2 *ctx, uint32_t pc)
{
	uint16_t inst;

	/* Table 5.2, Instruction Code Format
	 *
	 * "The actual number of cycles may be increased:
	 *  1. When contention occurs between instruction fetches and data
	 *     access, or
	 *  2. When the destination register of a load instruction and the
	 *     register used by the next instruction are the same.
	 *
	 * XXX emulate this.
	 */

	inst = cpu->fetch (cpu, pc);
	insns[inst] (ctx, inst);
	sh2_tick (ctx);
	vk_cpu (ctx)->remaining --;
}

static void
sh2_delay_slot (sh2 *ctx, uint32_t pc)
{
	ctx->in_slot = true;
	sh2_step (ctx, pc);
	ctx->in_slot = false;
}

static int
sh2_run (vk_cpu *cpu, int cycles)
{
	sh2 *ctx = sh2 (cpu);

	cpu->remaining = cycles;
	while (cpu->remaining > 0) {
		if (cpu->state != VK_CPU_STATE_RUN) {
			break;
		}
		sh2_process_irqs (cpu);
		sh2_step (ctx, PC);
		PC += 2;
	}

	return -cpu->remaining;
}

static void
sh2_set_state (vk_cpu *cpu, vk_cpu_state state)
{
	vk_cpu_state real_state = state;

	switch (state) {
	case VK_CPU_STATE_SLEEP:
		if (IREG8 (SBYCR) & 0x80) {
			LOG ("entering STANDBY mode");
			real_state = VK_CPU_STATE_STANDBY;
		} else {
			LOG ("entering SLEEP mode");
		break;
	default:
		break;
	}

	cpu->state = real_state;
}

static void
sh2_reset (vk_cpu *cpu)
{
	sh2 *ctx = (sh2 *) cpu;

	vk_cpu_reset (cpu);

	cpu->state = (ctx->master) ? VK_CPU_STATE_RUN : VK_CPU_STATE_STOP;

	memset (ctx->r, 0, sizeof (ctx->r));
	memset (ctx->ireg, 0, sizeof (ctx->ir));
	memset (ctx->irqs, 0, sizeof (ctx->irqs));

	PC	= R32 (0);
	R(15)	= R32 (4);
	PR	= 0;
	GBR	= 0;
	VBR	= 0;
	MAC	= 0;

	SR.val	= 0;
	SR.bit.i = 0xF;

	/* SCI */
	IREG8 (SCI_BRR) = 0xFF;
	IREG8 (SCI_TDR) = 0xFF;
	IREG8 (SCI_SSR) = 0x84;

	/* FRT */
	IREG8 (FRT_TIER) = 0x01;
	IREG8 (FRT_TOCR) = 0xE0;

	ctx->frt.frc = 0;
	ctx->frt.ocra = 0xFFFF;
	ctx->frt.ocrb = 0xFFFF;
	ctx->frt.icr = 0;

	/* WDT */
	IREG8 (WDT_WTCSR)  = 0x18;
	IREG8 (WDT_RSTCSR_W) = 0x1F;

	/* BSC */
	IREG16 (BSC_BCR1) = 0x03F0 | ((ctx->master) ? 0 : 0x8000);
	IREG16 (BSC_BCR2) = 0x00FC;
	IREG16 (BSC_WCR)  = 0xAAFF;

	IREG8 (SBYCR) = 0x60;

	ctx->irq_pending = false;
}

sh2 *
sh2_new (struct vk_machine *mach, vk_mmap *mmap, bool master)
{
	sh2 *ctx = ALLOC (sh2);
	vk_cpu *base;

	if (!ctx) {
		return NULL;
	}
	cpu = (vk_cpu *) ctx;

	cpu->mach		= mach;
	cpu->mmap		= mmap;

	cpu->set_state		= sh2_set_state;
	cpu->run		= sh2_run;
	cpu->reset		= sh2_reset;
	cpu->set_irq_state	= sh2_set_irq_state;
	cpu->process_irqs	= sh2_process_irqs;
	cpu->read		= sh2_read;
	cpu->write		= sh2_write;

	return (vk_cpu *) ctx;
}

void
sh2_delete (sh2 **ctx_)
{
	FREE (ctx_);
}

