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

#ifndef __SH4_H__
#define __SH4_H__

#include "vk/core.h"
#include "vk/buffer.h"
#include "vk/cpu.h"

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

typedef enum {
	SH4_IESOURCE_NMI,
	/* IRQs */
	SH4_IESOURCE_IRQ0,
	SH4_IESOURCE_IRQ1,
	SH4_IESOURCE_IRQ2,
	SH4_IESOURCE_IRQ3,
	SH4_IESOURCE_IRQ4,
	SH4_IESOURCE_IRQ5,
	SH4_IESOURCE_IRQ6,
	SH4_IESOURCE_IRQ7,
	SH4_IESOURCE_IRQ8,
	SH4_IESOURCE_IRQ9,
	SH4_IESOURCE_IRQ10,
	SH4_IESOURCE_IRQ11,
	SH4_IESOURCE_IRQ12,
	SH4_IESOURCE_IRQ13,
	SH4_IESOURCE_IRQ14,
	/* IRLs */
	SH4_IESOURCE_IRL0,
	SH4_IESOURCE_IRL1,
	SH4_IESOURCE_IRL2,
	SH4_IESOURCE_IRL3,
	/* UDI */
	SH4_IESOURCE_UDI,
	/* GPIO */
	SH4_IESOURCE_GPIOI,
	/* DMAC */
	SH4_IESOURCE_DMTE0,
	SH4_IESOURCE_DMTE1,
	SH4_IESOURCE_DMTE2,
	SH4_IESOURCE_DMTE3,
	SH4_IESOURCE_DMAE,
	/* TMU */
	SH4_IESOURCE_TUNI0,
	SH4_IESOURCE_TUNI1,
	SH4_IESOURCE_TUNI2,
	SH4_IESOURCE_TICPI2,
	/* RTC */
	SH4_IESOURCE_ATI,
	SH4_IESOURCE_PRI,
	SH4_IESOURCE_CUI,
	/* SCI1 */
	SH4_IESOURCE_ERI,
	SH4_IESOURCE_RXI,
	SH4_IESOURCE_TXI,
	SH4_IESOURCE_TEI,
	/* SCIF */
	SH4_IESOURCE_ERIF,
	SH4_IESOURCE_RXIF,
	SH4_IESOURCE_BRIF,
	SH4_IESOURCE_TXIF,
	/* WDT */
	SH4_IESOURCE_ITI,
	/* REF */
	SH4_IESOURCE_RCMI,
	SH4_IESOURCE_ROVI,

	SH4_NUM_IESOURCES,
} sh4_iesource_t;

typedef struct {
	vk_irq_state_t state;
	unsigned priority;
	uint32_t offset;
	uint32_t code;
} sh4_irq_state_t;

typedef struct sh4_t sh4_t;

struct sh4_t {
	vk_cpu_t base;

	/* State */
	bool		in_slot;

	/* Registers */
	struct {
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
		union {
			alias32uf_t f[16];
			alias64uf_t d[8];
		} f, x;
		alias32uf_t	fpul;
		sh4_fpscr_t	fpscr;
	} regs;

	/* On-Chip Modules */
	vk_buffer_t	*iregs;

	struct {
		/* True if any interrupt is pending */
		bool pending;
		/* Index of the highest priority raised interrupt */
		int index;
		/* Overall interrupt state object */
		sh4_irq_state_t irqs[SH4_NUM_IESOURCES];
	} intc;

	struct {
		bool	is_running[4];
	} dmac;

	struct {
		bool	is_running[3];
		uint32_t counter[3];
	} tmu;

	struct {
		int	(* get)(sh4_t *ctx, uint16_t *val);
		int	(* put)(sh4_t *ctx, uint16_t val);
	} porta;

	/* Configuration */
	struct {
		bool	master;
		bool	little_endian;
	} config;
};

vk_cpu_t	*sh4_new (vk_machine_t *mach, vk_mmap_t *mmap, bool master, bool le);
void		 sh4_set_porta_handlers (vk_cpu_t *cpu,
		                         int (* get)(sh4_t *ctx, uint16_t *val),
		                         int (* put)(sh4_t *ctx, uint16_t val));

#endif /* __SH4_H__ */
