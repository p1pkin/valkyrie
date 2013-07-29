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

#ifndef __VK_SH_INSNS_INTERP_H__
#define __VK_SH_INSNS_INTERP_H__

#if !defined(I) || !defined(IDEF) || !(defined(IS_SH2) || defined(IS_SH4))
#error "required macro not defined"
#endif

typedef struct {
	uint16_t mask;
	uint16_t match;
	itype handler;
} idesctype;

static itype insns[65536];

I (invalid)
{
	VK_CPU_ABORT (ctx, "invalid instruction %04X", inst);
}

/****************************************************************************
 Data Move Instructions
****************************************************************************/

I (mov)
{
	RN = RM;
}

I (movi)
{
	RN = _SIMM8;
}

I (movwi)
{
	/* TODO: only add 2 in a delay slot */
	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	RN = (int32_t)(int16_t) R16 (ctx, (PC + 4) + (_UIMM8 << 1));
}

I (movli)
{
	/* TODO: only add 2 in a delay slot */
	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	RN = R32 (ctx, ((PC + 4) & ~3) + (_UIMM8 << 2));
}

I (movbs)
{
	W8 (ctx, RN, RM);
}

I (movws)
{
	W16 (ctx, RN, RM);
}

I (movls)
{
	W32 (ctx, RN, RM);
}

I (movbl)
{
	RN = (int32_t)(int8_t) R8 (ctx, RM);
}

I (movwl)
{
	RN = (int32_t)(int16_t) R16 (ctx, RM);
}

I (movll)
{
	RN = R32 (ctx, RM);
}

I (movbm)
{
	W8 (ctx, RN - 1, RM);
	RN--;
}

I (movwm)
{
	W16 (ctx, RN - 2, RM);
	RN -= 2;
}

I (movlm)
{
	W32 (ctx, RN - 4, RM);
	RN -= 4;
}

I (movbp)
{
	RN = (int32_t)(int8_t) R8 (ctx, RM);
	if (_RN != _RM) {
		RM ++;
	}
}

I (movwp)
{
	RN = (int32_t)(int16_t) R16 (ctx, RM);
	if (_RN != _RM) {
		RM += 2;
	}
}

I (movlp)
{
	RN = R32 (ctx, RM);
	if (_RN != _RM) {
		RM += 4;
	}
}

I (movbs0)
{
	W8 (ctx, RN + R0, RM);
}

I (movws0)
{
	W16 (ctx, RN + R0, RM);
}

I (movls0)
{
	W32 (ctx, RN + R0, RM);
}

I (movbl0)
{
	RN = (int32_t)(int8_t) R8 (ctx, RM + R0);
}

I (movwl0)
{
	RN = (int32_t)(int16_t) R16 (ctx, RM + R0);
}

I (movll0)
{
	RN = R32 (ctx, RM + R0);
}

I (movblg)
{
	R0 = (int32_t)(int8_t) R8 (ctx, GBR + _UIMM8);
}

I (movwlg)
{
	R0 = (int32_t)(int16_t) R16 (ctx, GBR + (_UIMM8 << 1));
}

I (movllg)
{
	R0 = R32 (ctx, GBR + (_UIMM8 << 2));
}

I (movbsg)
{
	W8 (ctx, GBR + _UIMM8, R0);
}

I (movwsg)
{
	W16 (ctx, GBR + (_UIMM8 << 1), R0);
}

I (movlsg)
{
	W32 (ctx, GBR + (_UIMM8 << 2), R0);
}

I (movbl4)
{
	R0 = (int32_t)(int8_t) R8 (ctx, RM + (inst & 15));
}

I (movwl4)
{
	R0 = (int32_t)(int16_t) R16 (ctx, RM + ((inst & 15) << 1));
}

I (movll4)
{
	RN = R32 (ctx, RM + ((inst & 15) << 2));
}

I (movbs4)
{
	W8 (ctx, RM + (inst & 15), R0);
}

I (movws4)
{
	W16 (ctx, RM + (inst & 15) * 2, R0);
}

I (movls4)
{
	W32 (ctx, RN + (inst & 15) * 4, RM);
}

I (mova)
{
	/* FIXME: only add 2 in a delay slot */
	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	R0 = ((PC + 4) & ~3) + (_UIMM8 * 4);
}

I (movt)
{
	RN = T;
}

I (swapb)
{
	RN = (RM & 0xFFFF0000) | ((RM << 8) & 0xFF00) | ((RM >> 8) & 0xFF);
}

I (swapw)
{
	RN = (RM >> 16) | (RM << 16);
}

I (xtrct)
{
	RN = (RM << 16) | (RN >> 16);
}

/****************************************************************************
 Arithmetical Instructions
****************************************************************************/

I (add)
{
	RN += RM;
}

I (addi)
{
	RN += _SIMM8;
}

I (addc)
{
	uint32_t tmp0, tmp1;

	tmp0 = RN;
	tmp1 = RN + RM;
	RN = tmp1 + T;

	T = (tmp0 > tmp1) || (tmp1 > RN);
}

I (addv)
{
	bool d, s, a;

	d = ((int32_t) RN) < 0;
	s = ((int32_t) RM) < 0;

	RN += RM;

	a = ((int32_t) RN) < 0;

	T = (d == s) && (d != a);
}

I (neg)
{
	RN = -RM;
}

I (negc)
{
	uint32_t tmp;

	tmp = -RM;
	RN = tmp - T;

	T = (0 < tmp) || (tmp < RN);
}

I (sub)
{
	RN -= RM;
}

I (subc)
{
	uint32_t tmp0, tmp1;

	tmp0 = RN;
	tmp1 = RN - RM;
	RN = tmp1 - T;

	T = (tmp0 < tmp1) || (tmp1 < RN);
}

I (subv)
{
	bool d, s, a;

	d = ((int32_t) RN) < 0;
	s = ((int32_t) RM) < 0;

	RN -= RM;

	a = ((int32_t) RN) < 0;

	T = (s != d) && (d != a);
}

I (cmpeq)
{
	T = (RN == RM);
}

I (cmpge)
{
	T = ((int32_t) RN >= (int32_t) RM);
}

I (cmpgt)
{
	T = ((int32_t) RN > (int32_t) RM);
}

I (cmphi)
{
	T = (RN > RM);
}

I (cmphs)
{
	T = (RN >= RM);
}

I (cmppz)
{
	T = ((int32_t) RN >= 0);
}

I (cmppl)
{
	T = ((int32_t) RN > 0);
}

I (cmpim)
{
	T = ((int32_t) R0 == _SIMM8);
}

I (cmpstr)
{
	uint32_t tmp;
	int32_t hh, hl, lh, ll;

	tmp = RN ^ RM;

	hh = (tmp >> 24) & 0xFF;
	hl = (tmp >> 16) & 0xFF;
	lh = (tmp >>  8) & 0xFF;
	ll = tmp & 0xFF;

	T = ((hh && hl && lh && ll) == 0);
}

I (div0s)
{
	Q = ((int32_t) RN) < 0;
	M = ((int32_t) RM) < 0;
	T = Q ^ M;
}

I (div0u)
{
	Q = 0;
	M = 0;
	T = 0;
}

I (div1)
{
	uint32_t tmp0, tmp1, tmp2, tmpq;

	tmpq = Q;
	Q = RN >> 31;

	RN = (RN << 1) | T;

	tmp0 = RN;
	tmp2 = RM;

	switch (tmpq) {
	case 0:
		switch (M) {
		case 0:
			RN -= tmp2;
			tmp1 = (RN > tmp0);
			Q = (Q == 0) ? tmp1 : !tmp1;
			break;
		case 1:
			RN += tmp2;
			tmp1 = (RN < tmp0);
			Q = (Q == 0) ? !tmp1 : tmp1;
			break;
		}
		break;
	case 1:
		switch (M) {
		case 0:
			RN += tmp2;
			tmp1 = (RN < tmp0);
			Q = (Q == 0) ? tmp1 : (tmp1 == 0);
			break;
		case 1:
			RN -= tmp2;
			tmp1 = (RN > tmp0);
			Q = (Q == 0) ? (tmp1 == 0) : tmp1;
			break;
		}
		break;
	}

	T = (Q == M);
}

I (dmuls)
{
	MAC = ((int64_t)(int32_t) RN) * ((int64_t)(int32_t) RM);
}

I (dmulu)
{
	MAC = ((uint64_t)(uint32_t) RN) * ((uint64_t)(uint32_t) RM);
}

I (dt)
{
	T = (--RN == 0);
}

I (extsb)
{
	RN = (int32_t)(int8_t) RM;
}

I (extsw)
{
	RN = (int32_t)(int16_t) RM;
}

I (extub)
{
	RN = (uint8_t) RM;
}

I (extuw)
{
	RN = (uint16_t) RM;
}

/*
 * XXX simplify MAC.[LW] to decent C code
 */

I (macl)
{
	uint32_t tmp0, tmp1, tmp2, tmp3;
	uint32_t rnh, rnl, rmh, rml;
	uint32_t res0, res1, res2;
	int32_t tmpn, tmpm, fnlml;

	tmpn = R32 (ctx, RN);
	RN += 4;

	tmpm = R32 (ctx, RM);
	RM += 4;

	fnlml = ((int32_t) (tmpn ^ tmpm) < 0) ? -1 : 0;

	tmpn = abs (tmpn);
	tmpm = abs (tmpm);

	tmp1 = (uint32_t) tmpn;
	tmp2 = (uint32_t) tmpm;

	rnl = tmp1 & 0xFFFF;
	rnh = (tmp1 >> 16) & 0xFFFF;
	rml = tmp2 & 0xFFFF;
	rmh = (tmp2 >> 16) & 0xFFFF;

	tmp0 = rml * rnl;
	tmp1 = rmh * rnl;
	tmp2 = rml * rnh;
	tmp3 = rmh * rnh;

	res1 = tmp1 + tmp2;
	res2 = (res1 >= tmp1) ? 0 : 0x10000;

	tmp1 = (res1 << 16);

	res0 = tmp0 + tmp1;
	res2 += (res0 < tmp0) ? 1 : 0;

	res2 += (res1 >> 16) + tmp3;

	if (fnlml < 0) {
		res2 = ~res2;
		if (res0 == 0) {
			res2 ++;
		} else {
			res0 = ~res0 + 1;
		}
	}

	if (S == 1) {
		VK_CPU_ASSERT (ctx, false);
		res0 = MACL + res0;
		if (MACL > res0) {
			res2 ++;
		}
		res2 += MACH & 0xFFFF;
		if (((int32_t) res2 < 0) && (res2 < 0xFFFF8000)) {
			res2 = 0x8000;
			res0 = 0;
		} else if (((int32_t) res2 > 0) && (res2 > 0x00007FFF)) {
			res2 = 0x00007FFF;
			res0 = 0xFFFFFFFF;
		}
	} else {
		res0 = MACL + res0;
		if (MACL > res0) {
			res2 ++;
		}
		res2 += MACH;
	}

	MACH = res2;
	MACL = res0;
}

I (macw)
{
	int32_t tmpm, tmpn, dst, src, ans;
	uint32_t tmpl;

	tmpn = R16 (ctx, RN);
	RN += 2;

	tmpm = R16 (ctx, RM);
	RM += 2;

	tmpl = MACL;
	tmpm = ((int32_t)(int16_t) tmpn) * ((int32_t)(int16_t) tmpm);

	dst = ((int32_t) MACL >= 0) ? 0 : 1;
	src = ((int32_t) tmpm >= 0) ? 0 : 1;
	tmpn = ((int32_t) tmpm >= 0) ? 0 : -1;

	src += dst;
	MACL += tmpm;

	ans = ((int32_t) MACL >= 0) ? 0 : 1;

	ans += dst;

	if (S == 1) {
		if (ans == 1) {
			if (src == 0) {
				MACL = 0x7FFFFFFF;
			}
			if (src == 2) {
				MACL = 0x80000000;
			}
		}
	} else {
		MACH += tmpn;
		if (tmpl > MACL) {
			MACH += 1;
		}
	}
}

I (mull)
{
	MACL = RN * RM;
}

I (mulsw)
{
	MACL = ((int32_t)(int16_t) RN) * ((int32_t)(int16_t) RM);
}

I (muluw)
{
	MACL = ((uint32_t)(uint16_t) RN) * ((uint32_t)(uint16_t) RM);
}

/****************************************************************************
 Logical Instructions
****************************************************************************/

I (and)
{
	RN &= RM;
}

I (andi)
{
	R0 &= _UIMM8;
}

I (andm)
{
	W8 (ctx, GBR + R0, R8 (ctx, GBR + R0) & _UIMM8);
}

I (not)
{
	RN = ~RM;
}

I (or)
{
	RN |= RM;
}

I (ori)
{
	R0 |= _UIMM8;
}

I (orm)
{
	W8 (ctx, GBR + R0, R8 (ctx, GBR + R0) | _UIMM8);
}

I (tas)
{
	uint8_t tmp;
	tmp = R8 (ctx, RN);
	T = (tmp == 0);
	W8 (ctx, RN, tmp | 0x80);
}

I (tst)
{
	T = ((RN & RM) == 0);
}

I (tsti)
{
	T = ((R0 & _UIMM8) == 0);
}

I (tstm)
{
	T = ((R8 (ctx, GBR + R0) & _UIMM8) == 0);
}

I (xor)
{
	RN ^= RM;
}

I (xori)
{
	R0 ^= _UIMM8;
}

I (xorm)
{
	W8 (ctx, GBR + R0, R8 (ctx, GBR + R0) ^ _UIMM8);
}

/****************************************************************************
 Rotate Shift Instructions
****************************************************************************/

I (rotl)
{
	T = RN >> 31;
	RN = (RN << 1) | T;
}

I (rotr)
{
	T = RN & 1;
	RN = (RN >> 1) | (T << 31);
}

I (rotcl)
{
	uint32_t t;

	t = RN >> 31;
	RN = (RN << 1) | T;
	T = t;
}

I (rotcr)
{
	uint32_t t;

	t = RN & 1;
	RN = (RN >> 1) | (T << 31);
	T = t;
}

I (shal)
{
	T = RN >> 31;
	RN <<= 1;
}

I (shar)
{
	T = RN & 1;
	RN = (((int32_t) RN) >> 1);
}

I (shll)
{
	T = RN >> 31;
	RN <<= 1;
}

I (shlr)
{
	T = RN & 1;
	RN >>= 1;
}

I (shll2)
{
	RN <<= 2;
}

I (shll8)
{
	RN <<= 8;
}

I (shll16)
{
	RN <<= 16;
}

I (shlr2)
{
	RN >>= 2;
}

I (shlr8)
{
	RN >>= 8;
}

I (shlr16)
{
	RN >>= 16;
}

/****************************************************************************
 Branch Instructions
****************************************************************************/

I (bt)
{
	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	if (T != 0) {
		PC = PC + (_SIMM8 << 1) + 4;
		PC = PC - 2;
	}
}

I (bf)
{
	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	if (T == 0) {
		PC = PC + (_SIMM8 << 1) + 4;
		PC = PC - 2;
	}
}

I (bts)
{
	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	if (T != 0) {
		uint32_t pc;
		pc = PC;
		PC = PC + (_SIMM8 << 1) + 4;
		delay_slot (ctx, pc + 2);
		PC = PC - 2;
	}
}

I (bfs)
{
	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	if (T == 0) {
		uint32_t pc;
		pc = PC;
		PC = PC + (_SIMM8 << 1) + 4;
		delay_slot (ctx, pc + 2);
		PC = PC - 2;
	}
}

I (bra)
{
	uint32_t pc;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);

	pc = PC;
	PC = PC + (_SIMM12 << 1) + 4;

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (braf)
{
	uint32_t pc = PC;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);

	PC = PC + RN + 4;

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (bsr)
{
	uint32_t pc;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);

	pc = PC;
	PR = PC + 4;
	PC = PC + (_SIMM12 << 1) + 4;

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (bsrf)
{
	uint32_t pc;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);

	pc = PC;
	PR = PC + 4;
	PC = PC + (int32_t) RN + 4;

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (jmp)
{
	uint32_t pc;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);
	VK_CPU_ASSERT (ctx, !(RN & 1));

	pc = PC;
	PC = RN;

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (jsr)
{
	uint32_t pc;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);

	pc = PC;
	PC = RN;
	PR = pc + 4;

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (rts)
{
	uint32_t pc;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);

	pc = PC;
	PC = PR;// + 4;

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

/****************************************************************************
 System Control Instructions
****************************************************************************/

I (clrt)
{
	T = 0;
}

I (sett)
{
	T = 1;
}

I (clrmac)
{
	MAC = 0;
}

I (ldcsr)
{
	set_sr (ctx, RN);
}

I (ldcgbr)
{
	GBR = RN;
}

I (ldcvbr)
{
	VBR = RN;
}

I (ldcmsr)
{
	set_sr (ctx, R32 (ctx, RN));
	RN += 4;
}

I (ldcmgbr)
{
	GBR = R32 (ctx, RN);
	RN += 4;
}

I (ldcmvbr)
{
	VBR = R32 (ctx, RN);
	RN += 4;
}

I (ldsmach)
{
	MACH = RN;
}

I (ldsmacl)
{
	MACL = RN;
}

I (ldspr)
{
	PR = RN;
}

I (ldsmmach)
{
	MACH = R32 (ctx, RN);
	RN += 4;
}

I (ldsmmacl)
{
	MACL = R32 (ctx, RN);
	RN += 4;
}

I (ldsmpr)
{
	PR = R32 (ctx, RN);
	RN += 4;
}

I (nop)
{
}

I (rte)
{
	uint32_t pc;

	VK_CPU_ASSERT (ctx, !ctx->in_slot);

#ifdef IS_SH2
	pc = PC;
	PC = R32 (ctx, SP);
	SP += 4;
	set_sr (ctx, R32 (ctx, SP));
	SP += 4;
#endif
#ifdef IS_SH4
	pc = PC;
	PC = SPC;
	set_sr (ctx, SSR.full);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (sleep)
{
	vk_cpu_set_state ((vk_cpu_t *) ctx, VK_CPU_STATE_SLEEP);
}

I (stcsr)
{
	RN = get_sr (ctx);
}

I (stcgbr)
{
	RN = GBR;
}

I (stcvbr)
{
	RN = VBR;
}

I (stcmsr)
{
	RN -= 4;
	W32 (ctx, RN, get_sr (ctx));
}

I (stcmgbr)
{
	RN -= 4;
	W32 (ctx, RN, GBR);
}

I (stcmvbr)
{
	RN -= 4;
	W32 (ctx, RN, VBR);
}

I (stsmach)
{
	RN = MACH;
}

I (stsmacl)
{
	RN = MACL;
}

I (stspr)
{
	RN = PR;
}

I (stsmmach)
{
	RN -= 4;
	W32 (ctx, RN, MACH);
}

I (stsmmacl)
{
	RN -= 4;
	W32 (ctx, RN, MACL);
}

I (stsmpr)
{
	RN -= 4;
	W32 (ctx, RN, PR);
}

I (trapa)
{
	VK_CPU_ABORT (ctx, "trapa");
}

#ifdef IS_SH4

#include <math.h>

/*****************************************************************************
 Arithmetic (SH-4)
*****************************************************************************/

I (shad)
{
	if (!(RM >> 31)) {
		RN <<= RM & 0x1F;
	} else if ((RM & 0x1F) == 0) {
		RN = (RN >> 31) ? 0xFFFFFFFF : 0;
	} else {
		RN = ((int32_t) RN) >> ((~RM & 0x1F) + 1);
	}
}

I (shld)
{
	if (!(RM >> 31)) {
		RN <<= RM & 0x1F;
	} else if ((RM & 0x1F) == 0) {
		RN = 0;
	} else {
		RN >>= ((~RM & 0x1F) + 1);
	}
}

/*****************************************************************************
 TLB, Cache, SQs (SH-4)
*****************************************************************************/

I (ldtlb)
{
}

I (movca)
{
	W32 (ctx, RN, R0);
}

I (ocbi)
{
}

I (ocbp)
{
}

I (ocbwb)
{
}

I (pref)
{
	//sync_store_queues (ctx);
}

/*****************************************************************************
 System Control (SH-4)
*****************************************************************************/

I (ldcssr)
{
	CHECK_PM
	SSR.full = RN;
}

I (ldcspc)
{
	CHECK_PM
	SPC = RN;
}

I (ldcdbr)
{
	CHECK_PM
	DBR = RN;
}

I (ldcrbank)
{
	CHECK_PM
	RBANK(_RM & 7) = RN;
}

I (stcssr)
{
	CHECK_PM
	RN = SSR.full;
}

I (stcspc)
{
	CHECK_PM
	RN = SPC;
}

I (stcsgr)
{
	CHECK_PM
	RN = SGR;
}

I (stcdbr)
{
	CHECK_PM
	RN = DBR;
}

I (stcrbank)
{
	CHECK_PM
	RN = RBANK(_RM & 7);
}

I (ldcmssr)
{
	CHECK_PM
	SSR.full = R32 (ctx, RN);
	RN += 4;
}

I (ldcmspc)
{
	CHECK_PM
	SPC = R32 (ctx, RN);
	RN += 4;
}

I (ldcmdbr)
{
	CHECK_PM
	DBR = R32 (ctx, RN);
	RN += 4;
}

I (ldcmrbank)
{
	CHECK_PM
	RBANK (_RM & 7) = R32 (ctx, RN);
	RN += 4;
}

I (stcmssr)
{
	CHECK_PM
	RN -= 4;
	W32 (ctx, RN, SSR.full);
}

I (stcmspc)
{
	CHECK_PM
	RN -= 4;
	W32 (ctx, RN, SPC);
}

I (stcmsgr)
{
	CHECK_PM
	RN -= 4;
	W32 (ctx, RN, SGR);
}

I (stcmdbr)
{
	CHECK_PM
	RN -= 4;
	W32 (ctx, RN, DBR);
}

I (stcmrbank)
{
	CHECK_PM
	RN -= 4;
	W32 (ctx, RN, RBANK (_RM & 7));
}

/*****************************************************************************
 Floating-Point
*****************************************************************************/

I (ldsfpscr)
{
	CHECK_FP
	set_fpscr (ctx, RN);
}

I (ldsmfpscr)
{
	CHECK_FP
	set_fpscr (ctx, R32 (ctx, RN));
	RN += 4;
}

I (stsfpscr)
{
	CHECK_FP
	RN = get_fpscr (ctx);
}

I (stsmfpscr)
{
	CHECK_FP
	RN -= 4;
	W32 (ctx, RN, get_fpscr (ctx));
}

I (ldsfpul)
{
	CHECK_FP
	FPUL.u = RN;
}

I (ldsmfpul)
{
	CHECK_FP
	FPUL.u = R32 (ctx, RN);
	RN += 4;
}

I (stsfpul)
{
	CHECK_FP
	RN = FPUL.u;
}

I (stsmfpul)
{
	CHECK_FP
	RN -= 4;
	W32 (ctx, RN, FPUL.u);
}

I (fschg)
{
	sh4_fpscr_t tmp;
	tmp.full = get_fpscr (ctx);
	tmp.bit.sz ^= 1;
	set_fpscr (ctx, tmp.full);
}

I (frchg)
{
	sh4_fpscr_t tmp;
	tmp.full = get_fpscr (ctx);
	tmp.bit.fr ^= 1;
	set_fpscr (ctx, tmp.full);
}

I (fldi0)
{
	VK_CPU_ASSERT (ctx, !FPSCR.bit.pr);
	FRN.f = 0.0f;
}

I (fldi1)
{
	VK_CPU_ASSERT (ctx, !FPSCR.bit.pr);
	FRN.f = 1.0f;
}

I (fsts)
{
	FRN.f = FPUL.f;
}

I (flds)
{
	FPUL.f = FRN.f;
}

I (flt)
{
	if (!FPSCR.bit.pr) {
		FRN.f = (float) (int32_t) FPUL.u; /* int32 to float */
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		DRN.f = (double) (int32_t) FPUL.u; /* int32 to double */
	}
}

I (ftrc)
{
	if (!FPSCR.bit.pr) {
		FPUL.u = (int32_t) FRN.f; /* float to int32 */
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		FPUL.u = (int32_t) DRN.f; /* double to int32 */
	}
}

I (fcnvsd)
{
	VK_CPU_ASSERT (ctx, !(_RN & 1));
	VK_CPU_ASSERT (ctx, !FPSCR.bit.pr);
	DRN.f = FPUL.f;
}

I (fcnvds)
{
	VK_CPU_ASSERT (ctx, !(_RN & 1));
	VK_CPU_ASSERT (ctx, !FPSCR.bit.pr);
	FPUL.f = DRN.f;
}

I (fmov)
{
	if (FPSCR.bit.sz | FPSCR.bit.pr) {
		uint64_t tmp;
		if (_RM & 1)
			tmp = XDM.u;
		else
			tmp = DRM.u;
		if (_RN & 1)
			XDN.u = tmp;
		else
			DRN.u = tmp;
	} else
		FRN.u = FRM.u;
}

I (fmov_load)
{
	if (!(FPSCR.bit.sz | FPSCR.bit.pr))
		FRN.u = R32 (ctx, RM);
	else if (_RN & 1)
		XDN.u = R64 (ctx, RM);
	else
		DRN.u = R64 (ctx, RM);
}

I (fmov_store)
{
	if (!(FPSCR.bit.sz | FPSCR.bit.pr))
		W32 (ctx, RN, FRM.u);
	else if (_RM & 1)
		W64 (ctx, RN, XDM.u);
	else
		W64 (ctx, RN, DRM.u);
}

I (fmov_index_load)
{
	if (!(FPSCR.bit.sz | FPSCR.bit.pr))
		FRN.u = R32 (ctx, R0 + RM);
	else if (_RN & 1)
		XDN.u = R64 (ctx, R0 + RM);
	else
		DRN.u = R64 (ctx, R0 + RM);
}

I (fmov_index_store)
{
	if (!(FPSCR.bit.sz | FPSCR.bit.pr))
		W32 (ctx, R0 + RN, FRM.u);
	else if (_RM & 1)
		W64 (ctx, R0 + RN, XDM.u);
	else
		W64 (ctx, R0 + RN, DRM.u);
}

I (fmov_restore)
{
	if (!(FPSCR.bit.sz | FPSCR.bit.pr)) {
		FRN.u = R32 (ctx, RM);
		RM += 4;
	} else if (_RN & 1) {
		XDN.u = R64 (ctx, RM);
		RM += 8;
	} else {
		DRN.u = R64 (ctx, RM);
		RM += 8;
	}
}

I (fmov_save)
{
	if (!(FPSCR.bit.sz | FPSCR.bit.pr)) {
		RN -= 4;
		W32 (ctx, RN, FRM.u);
	} else if (_RM & 1) {
		RN -= 8;
		W64 (ctx, RN, XDM.u);
	} else {
		RN -= 8;
		W64 (ctx, RN, DRM.u);
	}
}

I (fneg)
{
	if (!FPSCR.bit.pr) {
		FRN.f = -FRN.f;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		DRN.f = -DRN.f;
	}
}

I (fabs)
{
	if (!FPSCR.bit.pr) {
		FRN.u = FRN.u & 0x7FFFFFFF;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		DRN.u = DRN.u & 0x7FFFFFFFFFFFFFFFull;
	}
}

I (fadd)
{
	if (!FPSCR.bit.pr) {
		FRN.f += FRM.f;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		VK_CPU_ASSERT (ctx, !(_RM & 1));
		DRN.f += DRM.f;
	}
}

I (fsub)
{
	if (!FPSCR.bit.pr) {
		FRN.f -= FRM.f;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		VK_CPU_ASSERT (ctx, !(_RM & 1));
		DRN.f -= DRM.f;
	}
}

I (fmul)
{
	if (!FPSCR.bit.pr) {
		FRN.f *= FRM.f;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		VK_CPU_ASSERT (ctx, !(_RM & 1));
		DRN.f *= DRM.f;
	}
}

I (fdiv)
{
	if (!FPSCR.bit.pr) {
		FRN.f /= FRM.f;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		VK_CPU_ASSERT (ctx, !(_RM & 1));
		DRN.f /= DRM.f;
	}
}

I (fmac)
{
	VK_CPU_ASSERT (ctx, FPSCR.bit.pr == 0);
	FRN.f += FRM.f * FR(0).f;
}

static const float fsca_alpha = (2.0f * (float) M_PI) / 65536.0f;

I (fsca)
{
	float angle = (FPUL.u & 0xFFFF) * fsca_alpha;
	unsigned n = _RN & ~1;
	FR(n+0).f = sinf (angle);
	FR(n+1).f = cosf (angle);
}

I (fsrra)
{
	if (FRN.f < 0)
		return;
	FRN.f = 1.0f / sqrtf (FRN.f);
}

I (fsqrt)
{
	CHECK_FP
	if (!FPSCR.bit.pr) {
		FRN.f = sqrtf (FRN.f);
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		DRN.f = sqrt (DRN.f);
	}
}

I (fipr)
{
	CHECK_FP
//	if (FPSCR.bit.pr) {
		unsigned n = _RN & ~3;
		unsigned m = _RM & ~3;

		FR(n+3).f = FR(n+0).f * FR(m+0).f +
		            FR(n+1).f * FR(m+1).f +
		            FR(n+2).f * FR(m+2).f +
		            FR(n+3).f * FR(m+3).f;
//	} else {
//		VK_CPU_ASSERT (ctx, 0);
//	}
}

I (ftrv)
{
	CHECK_FP
//	if (FPSCR.bit.pr) {
		unsigned n = _RN & ~3;
		float res[4];

		res[0] = XF(0).f  * FR(n+0).f +
		         XF(4).f  * FR(n+1).f +
		         XF(8).f  * FR(n+2).f +
		         XF(12).f * FR(n+3).f;
		res[1] = XF(1).f  * FR(n+0).f +
		         XF(5).f  * FR(n+1).f +
		         XF(7).f  * FR(n+2).f +
		         XF(13).f * FR(n+3).f;
		res[2] = XF(2).f  * FR(n+0).f +
		         XF(6).f  * FR(n+1).f +
		         XF(10).f * FR(n+2).f +
		         XF(14).f * FR(n+3).f;
		res[3] = XF(3).f  * FR(n+0).f +
		         XF(7).f  * FR(n+1).f +
		         XF(11).f * FR(n+2).f +
		         XF(15).f * FR(n+3).f;

		FR(n+0).f = res[0];
		FR(n+1).f = res[1];
		FR(n+2).f = res[2];
		FR(n+3).f = res[3];
//	} else {
//		VK_CPU_ASSERT (ctx, 0);
//	}
}

I (fcmpeq)
{
	if (!FPSCR.bit.pr) {
		T = FRN.f == FRM.f;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		VK_CPU_ASSERT (ctx, !(_RM & 1));
		T = DRN.f == DRM.f;
	}
}

I (fcmpgt)
{
	if (!FPSCR.bit.pr) {
		T = FRN.f > FRM.f;
	} else {
		VK_CPU_ASSERT (ctx, !(_RN & 1));
		VK_CPU_ASSERT (ctx, !(_RM & 1));
		T = DRN.f > DRM.f;
	}
}

#endif /* IS_SH4 */

#endif /* __VK_SH_INSNS_INTERP_H__ */
