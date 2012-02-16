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

#ifndef __VK_SH4_H__
#define __VK_SH4_H__

#include "vk/types.h"
#include "vk/core.h"
#include "vk/buffer.h"
#include "vk/cpu.h"

#include "cpu/sh/sh4-mmu.h"

typedef union {
	struct {
		uint32_t t  : 1;
		uint32_t s  : 1;
		uint32_t    : 2;
		uint32_t i  : 4;
		uint32_t q  : 1;
		uint32_t m  : 1;
		uint32_t    : 5;
		uint32_t fd : 1;	/* FPU disable */
		uint32_t    : 12;
		uint32_t bl : 1;	/* Exception/Interrupt block */
		uint32_t rb : 1;	/* Register bank */
		uint32_t md : 1;	/* Processor mode */
		uint32_t    : 1;
	} bit;
	uint32_t full;
} sh4_sr_t;

typedef union {
	struct {
		uint32_t rm		: 2; /** Rounding mode */
		uint32_t flag		: 5; /** Exception flag */
		uint32_t enable		: 5; /** Exception enable */
		uint32_t cause		: 6; /** Exception cause */
		uint32_t dn		: 1; /** Denormalization mode */
		uint32_t pr		: 1; /** Precision mode */
		uint32_t sz		: 1; /** Transfer size */
		uint32_t fr		: 1; /** Floating-point register bank */
	} bit;
	uint32_t full;
} sh4_fpscr_t;

typedef struct {
	union {
		uint64_t tc   : 1;	/**< Timing control */
		uint64_t sa   : 3;	/**< Space attributes (PCMCIA only) */
		uint64_t pr   : 1;	/**< Protection key data */
		uint64_t c    : 1;	/**< Cacheable */
		uint64_t sh   : 1;	/**< Shared */
		uint64_t sz   : 2;	/**< Page size */
		uint64_t ppn  : 19;	/**< Physical page number */
		uint64_t v    : 1;	/**< Valid */
		uint64_t vpn  : 22;	/**< Virtual page number */
		uint64_t asid : 8;	/**< Address space identifier */
	} part;
	uint64_t full;
} sh4_itlb_entry_t;

typedef struct {
	union {
		uint64_t tc   : 1;	/**< Timing control */
		uint64_t sa   : 3;	/**< Space attributes (PCMCIA only) */
		uint64_t wt   : 1;	/**< Write-through */
		uint64_t d    : 1;	/**< Dirty */
		uint64_t pr   : 2;	/**< Protection key data */
		uint64_t c    : 1;	/**< Cacheability */
		uint64_t sh   : 1;	/**< Shared  */
		uint64_t sz   : 2;	/**< Page size */
		uint64_t ppn  : 19;	/**< Physical page number */
		uint64_t v    : 1;	/**< Valid */
		uint64_t vpn  : 22;	/**< Virtual page number */
		uint64_t asid : 8;	/**< Address space idenfitifier */
	} part;
	uint64_t full;
} sh4_utlb_entry_t;

#define NUM_ITLB_ENTRIES 4
#define NUM_UTLB_ENTRIES 64
#define NUM_SQ_ENTRIES 32

/* Possible IRQ sources include: 16 IRQs from the IRL pins, NMI, TMU, RTC,
 * SCI, SCIF, WDT, DMAC, UDI, GPIO and UDI */

typedef enum {
	SH4_IRQ_SOURCE_IRQ0,
	SH4_IRQ_SOURCE_NMI = 16,
	SH4_IRQ_SOURCE_TMU,
	SH4_IRQ_SOURCE_RTC,
	SH4_IRQ_SOURCE_SCI,
	SH4_IRQ_SOURCE_SCIF,
	SH4_IRQ_SOURCE_WDT,
	SH4_IRQ_SOURCE_REF,
	SH4_IRQ_SOURCE_DMAC,
	SH4_IRQ_SOURCE_UDI,
	SH4_IRQ_SOURCE_GPIO,

	SH4_NUM_IRQS,
} sh4_irq_source_t;

#define SH4_IRQ_SOURCE_IRQ(n) \
	(SH4_IRQ_SOURCE_IRQ0 + (n))

#define SH4_IRQ_SOURCE_IRL(n) \
	(SH4_IRQ_SOURCE_IRQ0 +

typedef union {
	struct {
		uint32_t de	: 1;
		uint32_t te	: 1;
		uint32_t ie	: 1;
		uint32_t	: 1;
		uint32_t ts	: 3;
		uint32_t tm	: 1;
		uint32_t rs	: 4;
		uint32_t sm	: 2;
		uint32_t dm	: 2;
		uint32_t al	: 1;
		uint32_t am	: 1;
		uint32_t rl	: 1;
		uint32_t ds	: 1;
		uint32_t	: 4;
		uint32_t dtc	: 1;
		uint32_t dsa	: 3;
		uint32_t src	: 1;
		uint32_t ssa	: 3;
	} bit;
	uint32_t full;
} sh4_chcr_t;

typedef union {
	struct {
		uint32_t dme	: 1;
		uint32_t nmif	: 1;
		uint32_t ae	: 1;
		uint32_t	: 5;
		uint32_t pr	: 2;
		uint32_t	: 5;
		uint32_t ddt	: 1;
	} bit;
	uint32_t full;
} sh4_dmaor_t;

typedef struct sh4_t sh4_t;

struct sh4_t {
	vk_cpu_t base;

	/* Registers */
	uint32_t	r[16];
	uint32_t	pc;
	sh4_sr_t	sr;
	uint32_t	pr;
	uint32_t	gbr;
	uint32_t	vbr;
	uint32_t	dbr;
	pair32u_t	mac;
	uint32_t	spc;
	sh4_sr_t	ssr;
	uint32_t	sgr;
	uint32_t	rbank[8];

	/* Floating-point registers */
	union {
		alias32uf_t f[16];
		alias64uf_t d[8];
	} f, x; /* XXX fixme */
	alias32uf_t	fpul;
	sh4_fpscr_t	fpscr;

	/* State */
	bool		in_slot;
	bool		irq_pending;
	vk_irq_t	irqs[17];

	/* MMU */
	sh4_itlb_entry_t	itlb[NUM_ITLB_ENTRIES];
	sh4_utlb_entry_t	utlb[NUM_UTLB_ENTRIES];

	/* Caches */
	uint8_t		icache[8*KB];
	uint8_t		ocache[16*KB];

	/* Store Queues */
	uint32_t	sq[2][NUM_SQ_ENTRIES];

	/* On-Chip Modules */
	vk_buffer_t	*iregs;

	/* DMA Controller (DMAC) */
	sh4_chcr_t	chcr[4];
	sh4_dmaor_t	dmaor;

	/* Port A */
	int		(* porta_get)(sh4_t *ctx, uint16_t *val);
	int		(* porta_put)(sh4_t *ctx, uint16_t val);

	/* Configuration */
	struct {
		bool	master;
		bool	enable_mmu;
		bool	enable_hw_mmu;
		bool	enable_cache;
		bool	enable_accurate_dma;
	} config;
};

/* sh4.c */
vk_cpu_t	*sh4_new (vk_machine_t *mach, vk_mmap_t *mmap, bool master);
void		 sh4_set_porta_handlers (vk_cpu_t *cpu,
		                         int (* get)(sh4_t *ctx, uint16_t *val),
		                         int (* put)(sh4_t *ctx, uint16_t val));

#endif /* __VK_SH4_H__ */
