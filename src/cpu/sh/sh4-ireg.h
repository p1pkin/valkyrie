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

#ifndef __VK_SH4_IREG_H__
#define __VK_SH4_IREG_H__

/* Cache Controller */
#define CCN_PTEH	0x000000
#define CCN_PTEL	0x000004
#define CCN_TTB		0x000008
#define CCN_TEA		0x00000C
#define CCN_MMUCR	0x000010
#define CCN_BASRA	0x000014
#define CCN_BASRB	0x000018
#define CCN_CCR		0x00001C
#define CCN_TRA		0x000020
#define CCN_EXPEVT	0x000024
#define CCN_INTEVT	0x000028
#define CCN_PTEA	0x000034
#define CCN_QACR0	0x000038
#define CCN_QACR1	0x00003C

/* User Break Controller */
#define UBC_BARA	0x200000
#define UBC_BAMRA	0x200004
#define UBC_BBRA	0x200008
#define UBC_BARB	0x20000C
#define UBC_BAMRB	0x200010
#define UBC_BBRB	0x200014
#define UBC_BDRB	0x200018
#define UBC_BDMRB	0x20001C
#define UBC_BRCR	0x200020

/* Bus Controller */
#define BSC_BCR1	0x800000
#define BSC_BCR2	0x800004
#define BSC_WCR1	0x800008
#define BSC_WCR2	0x80000C
#define BSC_WCR3	0x800010
#define BSC_MCR		0x800014
#define BSC_PCR		0x800018
#define BSC_RTCSR	0x80001C
#define BSC_RTCNT	0x800020
#define BSC_RTCOR	0x800024
#define BSC_RFCR	0x800028
#define BSC_PCTRA	0x80002C
#define BSC_PDTRA	0x800030
#define BSC_PCTRB	0x800040
#define BSC_PDTRB	0x800044
#define BSC_GPIOIC	0x800048

#define BSC_SDMR2	0x900000
#define BSC_SDMR3	0x940000

/* DMA Controller */
#define DMAC_SAR0	0xA00000
#define DMAC_DAR0	0xA00004
#define DMAC_TCR0	0xA00008
#define DMAC_CHCR0	0xA0000C
#define DMAC_SAR1	0xA00010
#define DMAC_DAR1	0xA00014
#define DMAC_TCR1	0xA00018
#define DMAC_CHCR1	0xA0001C
#define DMAC_SAR2	0xA00020
#define DMAC_DAR2	0xA00024
#define DMAC_TCR2	0xA00028
#define DMAC_CHCR2	0xA0002C
#define DMAC_SAR3	0xA00030
#define DMAC_DAR3	0xA00034
#define DMAC_TCR3	0xA00038
#define DMAC_CHCR3	0xA0003C
#define DMAC_DMAOR	0xA00040

/* Clock Pulse Generator */
#define CPG_FRQCR	0xC00000
#define CPG_STBCR	0xC00004
#define CPG_WTCNT	0xC00008
#define CPG_WTCSR	0xC0000C
#define CPG_STBCR2	0xC00010

/* Real-time Clock */
#define RTC_R64CNT	0xC80000
#define RTC_RSECCNT	0xC80004
#define RTC_RMINCNT	0xC80008
#define RTC_RHRCNT	0xC8000C
#define RTC_RWKCNT	0xC80010
#define RTC_RDAYCNT	0xC80014
#define RTC_RMONCNT	0xC80018
#define RTC_RYRCNT	0xC8001C
#define RTC_RSECAR	0xC80020
#define RTC_RMINAR	0xC80024
#define RTC_RHRAR	0xC80028
#define RTC_RWKAR	0xC8002C
#define RTC_RDAYAR	0xC80030
#define RTC_RMONAR	0xC80034
#define RTC_RCR1	0xC80038
#define RTC_RCR2	0xC8003C

/* Interrupt Controller */
#define INTC_ICR	0xD00000
#define INTC_IPRA	0xD00004
#define INTC_IPRB	0xD00008
#define INTC_IPRC	0xD0000C
#define INTC_IPRD	0xD00010 /* Probably not in the 7095... */

/* Timer Unit */
#define TMU_TOCR	0xD80000
#define TMU_TSTR	0xD80004
#define TMU_TCOR0	0xD80008
#define TMU_TCNT0	0xD8000C
#define TMU_TCR0	0xD80010
#define TMU_TCOR1	0xD80014
#define TMU_TCNT1	0xD80018
#define TMU_TCR1	0xD8001C
#define TMU_TCOR2	0xD80020
#define TMU_TCNT2	0xD80024
#define TMU_TCR2	0xD80028
#define TMU_TCPR2	0xD8002C

/* Serial Communication Interface */
#define SCI_SCSMR1	0xE00000
#define SCI_SCBRR1	0xE00004
#define SCI_SCSCR1	0xE00008
#define SCI_SCTDR1	0xE0000C
#define SCI_SCSSR1	0xE00010
#define SCI_SCRDR1	0xE00014
#define SCI_SCSCMR1	0xE00018
#define SCI_SCSPTR1	0xE0001C

/* Serial Communication Interface F */
#define SCIF_SCSMR2	0xE80000
#define SCIF_SCBRR2	0xE80004
#define SCIF_SCSCR2	0xE80008
#define SCIF_SCFTDR2	0xE8000C
#define SCIF_SCFSR2	0xE80010
#define SCIF_SCFRDR2	0xE80014
#define SCIF_SCFCR2	0xE80018
#define SCIF_SCFDR2	0xE8001C
#define SCIF_SCSPTR2	0xE80020
#define SCIF_SCLSR2	0xE80024

/* User Debugging Interface */
#define UDI_SDIR	0xF00000
#define UDI_SDDR	0xF00008

#endif /* __VK_SH4_IREG_H__ */

