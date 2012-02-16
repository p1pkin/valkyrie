
#include "cpu/sh/sh4.h"

typedef struct {
	union {
		uint32_t dme  : 1;
		uint32_t nmif : 1;
		uint32_t ae   : 1;
		uint32_t      : 1;
		uint32_t cod  : 1;
		uint32_t      : 3;
		uint32_t pr   : 2;
		uint32_t      : 5;
		uint32_t ddt  : 1;
		uint32_t      : 16;
	} part;
	uint32_t full;
} dmac_dmaor;

typedef struct {
	union {
		uint32_t de  : 1;
		uint32_t te  : 1;
		uint32_t ie  : 1;
		uint32_t     : 1;
		uint32_t ts  : 3;
		uint32_t tm  : 1;
		uint32_t rs  : 4;
		uint32_t sm  : 2;
		uint32_t dm  : 2;
		uint32_t al  : 1; /* Channels only */
		uint32_t am  : 1; /* Channels only */
		uint32_t rl  : 1; /* Channels only */
		uint32_t ds  : 1; /* Channels only */
		uint32_t     : 4;
		uint32_t dtc : 1; /* PCMCIA only */
		uint32_t dsa : 3; /* PCMCIA only */
		uint32_t stc : 1; /* PCMCIA only */
		uint32_t ssa : 3; /* PCMCIA only */
	} part;
	uint32_t full;
} dmac_chcr;

#define REG(a_)	ctx->dmac.regs[(a_) / 4]

static inline uint32_t
get_chan (uint32_t addr)
{
	return (addr >> 8) & 3;
}

/* From Table 14.3, "DMAC Registers":
 *
 * "Longword access should be used for all control registers. If a different
 *  access width is used, reads will return all 0s and writes will not be
 *  possible."
 */

int
sh4_dmac_read (sh4 *ctx, unsigned size, uint32_t addr, uint64_t *val)
{
	if (size == 4)
		*val = REGS (addr);
	else
		*val = 0;
	return 0;
}

int
sh4_dmac_write (sh4 *ctx, unsigned size, uint32_t addr, uint64_t val)
{
	if (size != 4)
		return 0;

	switch (addr) {
	case DMAC_SAR0:
	case DMAC_SAR1:
	case DMAC_SAR2:
	case DMAC_SAR3:
		if (get_chan (addr) && get_dmaor (ctx)->part.ddt) {
			/* In DDT mode, writes from the CPU are masked */
			break;
		}
		break;
		break;
	case DMAC_DAR0:
		break;
	case DMAC_DMACTR0:
		break;
	case DMAC_CHCR0: {
			uint32_t tmp = IREG_GET (4, DMAC_CHCR0);
			/* Bit 1 of AND'ed */
			tmp = (val & ~2) | (val & tmp & 2);
			IREG_PUT (4, DMAC_CHCR0, tmp);
		}
		break;
	case DMAC_DMAOR: {
			uint32_t tmp = IREG_GET (DMAC_DMAOR);
			/* Bits 1-2 are AND'ed */
			tmp = (val & ~6) | (val & tmp & 6);
			IREG_PUT (4, DMAC_DMAOR, tmp);
		}
		break;
	default:
		VK_ASSERT (0);
	}
}

/* in single-mode, 1 cycle per bus transfer;
 * in double-mode, 2 cycles per bus transfer */

static void
dmac_tick_channel (sh4 *ctx, unsigned ch)
{
	/* TODO */	
}

void
sh4_dmac_tick (sh4 *ctx)
{
	/* TODO: priority, DMAOR.pr */
	dmac_tick_channel (ctx, 0);
	dmac_tick_channel (ctx, 1);
	dmac_tick_channel (ctx, 2);
	dmac_tick_channel (ctx, 3);
}
