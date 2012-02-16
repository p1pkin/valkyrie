/*
 *  Valkyrie
 *  Copyright(C) 2011, Stefano Teso
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __VK_SH2_IREG_H__
#define __VK_SH2_IREG_H__

/* CAS Latency */
/* Not proper IREGs, but whatever */
#define CAS_LATENCY_0	0xFFFF8848
#define CAS_LATENCY_1	0xFFFF8888
#define CAS_LATENCY_2	0xFFFF88C8

/* Interrupt Controller */
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

/* User Break Control */
#define	UBC_BARAH	0xFFFFFF40
#define	UBC_BARAL	0xFFFFFF42
#define	UBC_BAMRAH	0xFFFFFF44
#define	UBC_BAMRAL	0xFFFFFF46
#define	UBC_BBRA	0xFFFFFF48
#define	UBC_BARBH	0xFFFFFF60
#define	UBC_BARBL	0xFFFFFF62
#define	UBC_BAMRBH	0xFFFFFF64
#define	UBC_BAMRBL	0xFFFFFF66
#define	UBC_BBRB	0xFFFFFF68
#define	UBC_BDRBH	0xFFFFFF70
#define	UBC_BDRBL	0xFFFFFF72
#define	UBC_BDMRBH	0xFFFFFF74
#define	UBC_BDMRBL	0xFFFFFF76
#define	UBC_BRCR	0xFFFFFF78

/* Bus Controller */
#define	BSC_BCR1	0xFFFFFFE0
#define	BSC_BCR2	0xFFFFFFE4
#define	BSC_WCR		0xFFFFFFE8
#define	BSC_MCR		0xFFFFFFEC
#define	BSC_RTCSR	0xFFFFFFF0
#define	BSC_RTCNT	0xFFFFFFF4
#define	BSC_RTCOR	0xFFFFFFF8

/* Cache Control */
#define	CCR_CCR		0xFFFFFE92

/* DMA Control */
#define	DMAC_SAR0	0xFFFFFF80
#define	DMAC_DAR0	0xFFFFFF84
#define	DMAC_TCR0	0xFFFFFF88
#define	DMAC_CHCR0	0xFFFFFF8C
#define	DMAC_DRCR0	0xFFFFFE71
#define	DMAC_SAR1	0xFFFFFF90
#define	DMAC_DAR1	0xFFFFFF94
#define	DMAC_TCR1	0xFFFFFF98
#define	DMAC_CHCR1	0xFFFFFF9C
#define	DMAC_DRCR1	0xFFFFFE72
#define	DMAC_DMAOR	0xFFFFFFB0

/* Division Unit */
#define	DIVU_DVSR	0xFFFFFF00
#define DIVU_DVSR2	0xFFFFFF20
#define	DIVU_DVDNT	0xFFFFFF04
#define DIVU_DVDNT2	0xFFFFFF24
#define	DIVU_DVCR	0xFFFFFF08
#define DIVU_DVCR2	0xFFFFFF28
#define	DIVU_DVDNTH	0xFFFFFF10
#define	DIVU_DVDNTH2	0xFFFFFF30
#define	DIVU_DVDNTH3	0xFFFFFF18
#define	DIVU_DVDNTH4	0xFFFFFF38
#define	DIVU_DVDNTL	0xFFFFFF14
#define	DIVU_DVDNTL2	0xFFFFFF34
#define	DIVU_DVDNTL3	0xFFFFFF1C
#define	DIVU_DVDNTL4	0xFFFFFF3C

/* Free-running Timer */
#define	FRT_TIER	0xFFFFFE10
#define	FRT_FTCSR	0xFFFFFE11
#define	FRT_FRCH	0xFFFFFE12
#define	FRT_FRCL	0xFFFFFE13
#define	FRT_OCRH	0xFFFFFE14
#define	FRT_OCRL	0xFFFFFE15
#define	FRT_TCR		0xFFFFFE16
#define	FRT_TOCR	0xFFFFFE17
#define	FRT_ICRH	0xFFFFFE18
#define	FRT_ICRL	0xFFFFFE19

/* Power-down Modes */
#define	SBYCR		0xFFFFFE91

/* Watchdog Timer */
#define	WDT_WTCSR	0xFFFFFE80
#define	WDT_WTCNT	0xFFFFFE81
#define WDT_RSTCSR_W	0xFFFFFE82
#define	WDT_RSTCSR_R	0xFFFFFE83

/* Serial Communication Interface */
#define	SCI_SMR		0xFFFFFE00
#define	SCI_BRR		0xFFFFFE01
#define	SCI_SCR		0xFFFFFE02
#define	SCI_TDR		0xFFFFFE03
#define	SCI_SSR		0xFFFFFE04
#define	SCI_RDR		0xFFFFFE05

#endif /* __VK_SH2_IREG_H__ */

