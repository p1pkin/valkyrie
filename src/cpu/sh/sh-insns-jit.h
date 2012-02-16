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

/* The SR flags are handled as separate registers. */

/* At the end of the bb we want to know whether it could possibly access some
 * functionality, so that we can lazily implement it (such as reading from
 * TMU regs.) */

#ifndef __VK_SH_INSNS_JIT_H__
#define __VK_SH_INSNS_JIT_H__

#ifndef J
#error "J macro not declared"
#endif

/*******************************************************************************
 Instructions
*******************************************************************************/

#define J(name) \
	static void sh2_translate_##name (sh2_jit *ctx, uint16_t inst)

/*******************************************************************************
 Data Move Instructions
*******************************************************************************/

#define JIT	ctx->jit

#define RVAL(n_) ctx->r[n_].val

#define R0VAL	RVAL(0)
#define RNVAL	RVAL(_RN)
#define RMVAL	RVAL(_RM)

T (mov)
{
	RNVAL = RMVAL;
}

I (movi)
{
	RNVAL = vk_jit_build_const_uint32 (_SIMM8);
}

T (movwi)
{
	uint32_t addr = ctx->ta + (ctx->in_delay_slot ? 2 : 4) + _UIMM8 * 2;
	RNVAL = vk_jit_build_read_direct (JIT, vk_cpu (ctx), 2, addr, VK_SIGNEXT_16_32);
}

T (movli)
{
	uint32_t addr = (ctx->ta + (ctx->in_delay_slot ? 2 : 4)) & ~3 + _UIMM8 * 4;
	RNVAL = vk_jit_build_read_direct (JIT, vk_cpu (ctx), 4, addr, VK_SIGNEXT_NONE);
}

T (movbs)
{
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 1, RNVAL, RMVAL);
}

T (movws)
{
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 2, RNVAL, RMVAL);
}

T (movls)
{
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 4, RNVAL, RMVAL);
}

T (movbl)
{
	RNVAL = vk_jit_build_read_indirect (JIT, vk_cpu (ctx), 1, RMVAL, VK_SIGNEXT_8_32);
}

T (movwl)
{
	RNVAL = vk_jit_build_read_indirect (JIT, vk_cpu (ctx), 2, RMVAL, VK_SIGNEXT_16_32);
}

T (movll)
{
	RNVAL = vk_jit_build_read_indirect (JIT, vk_cpu (ctx), 4, RMVAL, VK_SIGNEXT_NONE);
}

T (movbm)
{
	LLVMBuildRef temp;
	temp = LLVMBuildSub (jit->builder, RNVAL, vk_jit_build_const_uint32 (-1), "predec");
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 1, RNVAL, RMVAL);
	RNVAL = temp;
}

T (movwm)
{
	LLVMBuildRef temp;
	temp = LLVMBuildSub (jit->builder, RNVAL, vk_jit_build_const_uint32 (-2), "predec");
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 2, RNVAL, RMVAL);
	RNVAL = temp;
}

T (movlm)
{
	LLVMBuildRef temp;
	temp = LLVMBuildSub (jit->builder, RNVAL, vk_jit_build_const_uint32 (-4), "predec");
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 4, RNVAL, RMVAL);
	RNVAL = temp;
}

T (movbp)
{
	RNVAL = vk_jit_build_read_indirect (JIT, vk_cpu (ctx), 1, RMVAL, VK_SIGNEXT_8_32);
	if (_RN != _RM) {
		RMVAL = LLVMBuildAdd (jit->builder, RMVAL, vk_jit_build_const_uint32 (1), "postinc");
	}
}

T (movwp)
{
	RNVAL = vk_jit_build_read_indirect (JIT, vk_cpu (ctx), 2, RMVAL, VK_SIGNEXT_16_32);
	if (_RN != _RM) {
		RMVAL = LLVMBuildAdd (jit->builder, RMVAL, vk_jit_build_const_uint32 (2), "postinc");
	}
}

T (movlp)
{
	RNVAL = vk_jit_build_read_indirect (JIT, vk_cpu (ctx), 4, RMVAL, VK_SIGNEXT_NONE);
	if (_RN != _RM) {
		RMVAL = LLVMBuildAdd (jit->builder, RMVAL, vk_jit_build_const_uint32 (4), "postinc");
	}
}

T (movbs0)
{
	LLVMValueRef temp = LLVMBuildAdd (jit->builder, RNVAL, R0VAL, "");
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 1, temp, RMVAL);
}

T (movws0)
{
	LLVMValueRef temp = LLVMBuildAdd (jit->builder, RNVAL, R0VAL, "");
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 2, temp, RMVAL);
}

T (movls0)
{
	LLVMValueRef temp = LLVMBuildAdd (jit->builder, RNVAL, R0VAL, "");
	vk_jit_build_write_indirect (JIT, vk_cpu (ctx), 4, temp, RMVAL);
}

I (movbl0)
{
	RN = (int32_t)(int8_t) R8 (RM + R0);
}

I (movwl0)
{
	RN = (int32_t)(int16_t) R16 (RM + R0);
}

I (movll0)
{
	RN = R32 (RM + R0);
}

I (movblg)
{
	R0 = (int32_t)(int8_t) R8 (GBR + _UIMM8);
}

I (movwlg)
{
	R0 = (int32_t)(int16_t) R16 (GBR + (_UIMM8 << 1));
}

I (movllg)
{
	R0 = R32 (GBR + (_UIMM8 << 2));
}

I (movbsg)
{
	W8 (GBR + _UIMM8, R0);
}

I (movwsg)
{
	W16 (GBR + (_UIMM8 << 1), R0);
}

I (movlsg)
{
	W32 (GBR + (_UIMM8 << 2), R0);
}

I (movbl4)
{
	R0 = (int32_t)(int8_t) R8 (RM + (inst & 15));
}

I (movwl4)
{
	R0 = (int32_t)(int16_t) R16 (RM + ((inst & 15) << 1));
}

I (movll4)
{
	RN = R32 (RM + ((inst & 15) << 2));
}

I (movbs4)
{
	W8 (RM + (inst & 15), R0);
}

I (movws4)
{
	W16 (RM + (inst & 15) * 2, R0);
}

I (movls4)
{
	W32 (RN + (inst & 15) * 4, RM);
}

T (mova)
{
	uint32_t addr = ((ctx->ta + (ctx->is_delay_slot ? 2 : 4)) & ~3) + _UIMM8 * 4;
	R0VAL = vk_jit_build_const_uint32 (JIT, addr);
}

T (movt)
{
	RNVAL = TVAL;
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

/******************************************************************************/
/* Arithmetical Instructions                                                  */
/******************************************************************************/

T (add)
{
	RNVAL = LLVMBuildAdd (jit->builder, RNVAL, RMVAL);
}

T (addr)
{
	LLVMValueRef imm = vk_build_const_uint32 (JIT, _SIMM8);
	RNVAL = LLVMBuildAdd (jit->builder, RNVAL, imm);
}

T (addc)
{
	LLVMValueRef temp0, temp1;

	temp0 = RNVAL;
	temp1 = LLVMBuildAdd (jit->builder, RNVAL, RMVAL);
	RNVAL = LLVMBuildAdd (jit->builder, temp1, TVAL);

	TVAL = 
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

#define CMP(cond_) \
	{ \
		LLVMValueRef temp = LLVMInt32TypeInContext (JIT->context); \
		LLVMValueRef cond = LLVMBuildICmp (ctx->builder, cond_, RNVAL, RMVAL); \
		TVAL = LLVMBuildZExt (ctx->builder, temp, cond, ""); \
	}

#define CMPI(cond_, imm_) \
	{ \
		LLVMValueRef temp = LLVMInt32TypeInContext (JIT->context); \
		LLVMValueRef imm  = vk_build_const_uint32 (JIT, imm_);
		LLVMValueRef cond = LLVMBuildICmp (ctx->builder, cond_, RNVAL, imm); \
		TVAL = LLVMBuildZExt (ctx->builder, temp, cond, "");
	}

T (cmpeq)
{
	CMP (LLVMIntEq);
}

T (cmpge)
{
	CMP (LLVMIntSGE);
}

T (cmpgt)
{
	CMP (LLVMIntSGT);
}

T (cmphi)
{
	CMP (LLVMIntUGT);
}

T (cmphs)
{
	CMP (LLVMIntUGE);
}

T (cmppz)
{
	CMPI (LLVMIntSGE, 0);
}

T (cmppl)
{
	CMPI (LLVMIntSGT, 0);
}

I (cmpim)
{
	CMPI (LLVMIntEq, _SIMM8);
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

I (macl)
{
	uint32_t tmp0, tmp1, tmp2, tmp3;
	uint32_t rnh, rnl, rmh, rml;
	uint32_t res0, res1, res2;
	int32_t tmpn, tmpm, fnlml;

	tmpn = R32 (RN);
	RN += 4;

	tmpm = R32 (RM);
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
		assert (0);
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

	tmpn = R16 (RN);
	RN += 2;

	tmpm = R16 (RM);
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

/*
 * Logical Instructions
 */

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
	W8 (GBR + R0, R8 (GBR + R0) & _UIMM8);
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
	W8 (GBR + R0, R8 (GBR + R0) | _UIMM8);
}

I (tas)
{
	uint8_t tmp;
	tmp = R8 (RN);
	T = (tmp == 0);
	W8 (RN, tmp | 0x80);
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
	T = ((R8 (GBR + R0) & _UIMM8) == 0);
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
	W8 (GBR + R0, R8 (GBR + R0) ^ _UIMM8);
}

/*
 * Rotate and Shift Instructions
 */

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

/*
 * Branch Instructions
 */

I (bt)
{
	assert (!ctx->in_slot);
	if (T != 0) {
		PC = PC + (_SIMM8 << 1) + 4;
		PC = PC - 2;
	}
}

I (bf)
{
	assert (!ctx->in_slot);
	if (T == 0) {
		PC = PC + (_SIMM8 << 1) + 4;
		PC = PC - 2;
	}
}

I (bts)
{
	assert (!ctx->in_slot);
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
	assert (!ctx->in_slot);
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

	assert (!ctx->in_slot);

	pc = PC;
	PC = PC + (_SIMM12 << 1) + 4;

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X -> %08X [bra]\n", pc, PC);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (braf)
{
	uint32_t pc = PC;

	assert (!ctx->in_slot);

	PC = PC + RN + 4;

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X -> %08X [braf]\n", pc, PC);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (bsr)
{
	uint32_t pc;

	assert (!ctx->in_slot);

	pc = PC;
	PR = PC + 4;
	PC = PC + (_SIMM12 << 1) + 4;

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X -> %08X [bsr]\n", pc, PC);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (bsrf)
{
	uint32_t pc;

	assert (!ctx->in_slot);

	pc = PC;
	PR = PC + 4;
	PC = PC + (int32_t) RN + 4;

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X -> %08X [bsrf]\n", pc, PC);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (jmp)
{
	uint32_t pc;

	assert (!ctx->in_slot);
	assert (!(RN & 1));

	pc = PC;
	PC = RN;

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X -> %08X [jmp]\n", pc, PC);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (jsr)
{
	uint32_t pc;

	assert (!ctx->in_slot);

	pc = PC;
	PC = RN;
	PR = pc + 4;

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X -> %08X [jsr]\n", pc, PC);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (rts)
{
	uint32_t pc;

	assert (!ctx->in_slot);

	pc = PC;
	PC = PR;// + 4;

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X <- %08X [rts]\n", PC, pc);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

/*
 * System Control Instructions
 */

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
	ctx->set_sr (ctx, RN);
}

I (ldcgbr)
{
	GBR = RN;
	assert ((GBR & 0x1FFFFFFF) == 0x0C00F000);
}

I (ldcvbr)
{
	VBR = RN;
}

I (ldcmsr)
{
	ctx->set_sr (ctx, R32 (RN));
	RN += 4;
}

I (ldcmgbr)
{
	GBR = R32 (RN);

	if ((GBR & 0x1FFFFFFF) != 0x0C00F000) {
		sh_println (stdout, ctx, "Warning: GBR changed to %08X\n", GBR);
	}

	RN += 4;
}

I (ldcmvbr)
{
	VBR = R32 (RN);
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
	MACH = R32 (RN);
	RN += 4;
}

I (ldsmmacl)
{
	MACL = R32 (RN);
	RN += 4;
}

I (ldsmpr)
{
	PR = R32 (RN);
	RN += 4;
}

I (nop)
{
}

I (rte)
{
	uint32_t pc;

	assert (!ctx->in_slot);

	pc = ctx->do_rte (ctx);

#ifdef SH2_LOG_JUMPS
	sh_println (stdout, ctx, "JUMP %08X -> %08X [rte]\n", pc, PC);
#endif

	delay_slot (ctx, pc + 2);
	PC = PC - 2;
}

I (sleep)
{
	ctx->sleep (ctx);
}

I (stcsr)
{
	RN = ctx->get_sr (ctx);
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
	W32 (RN, ctx->get_sr (ctx));
}

I (stcmgbr)
{
	RN -= 4;
	W32 (RN, GBR);
}

I (stcmvbr)
{
	RN -= 4;
	W32 (RN, VBR);
}

I (stcmssr)
{
	RN -= 4;
	W32 (RN, SSR.val);
}

I (ldcmssr)
{
	SSR.val = R32 (RN);
	RN += 4;
}

I (stcmspc)
{
	RN -= 4;
	W32 (RN, SPC);
}

I (ldcmspc)
{
	SPC = R32 (RN);
	RN += 4;
}

I (stcmrbank)
{
	RN -= 4;
	W32 (RN, RBANK (RM & 7));
}

I (ldcmrbank)
{
	RBANK (RM & 7) = R32 (RN);
	RN += 4;
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
	W32 (RN, MACH);
}

I (stsmmacl)
{
	RN -= 4;
	W32 (RN, MACL);
}

I (stsmpr)
{
	RN -= 4;
	W32 (RN, PR);
}

I (trapa)
{
	sh_println (stdout, ctx, "ERROR: trapa\n");
	assert (0);
}

/*******************************************************************************
 SH4: System/Control Instructions
*******************************************************************************/

// TODO: port to ldcsr, etc.

#define CHECK_PRIV	assert (ctx->model == SH_MODEL_SH2 || SR.bit.md);
#define CHECK_FP	assert (SR.bit.fd == 0);

I (ldcssr)
{
	CHECK_PRIV
	SSR.val = RN;
}

I (ldcspc)
{
	CHECK_PRIV
	SPC = RN;
}

I (ldcdbr)
{
	CHECK_PRIV
	DBR = RN;
}

I (ldcrbank)
{
	CHECK_PRIV
	assert(0);
}

I (ldcmdbr)
{
	CHECK_PRIV
	assert(0);
}

I (ldtlb)
{
	assert(0);
}

I (movca)
{
	W32 (RN, R0);
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
	// TODO: on-chip store queue exec
}

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

/*******************************************************************************
 SH4: Floating-Point Instructions
*******************************************************************************/

I (ldsfpscr)
{
	bool old_fr = FPSCR.bit.fr;

	CHECK_FP
	FPSCR.val = RN & 0x003FFFFF;

	assert (FPSCR.bit.fr == old_fr);
}

I (ldsmfpscr)
{
	bool old_fr = FPSCR.bit.fr;

	CHECK_FP
	FPSCR.val = R32 (RN) & 0x003FFFFF;
	RN += 4;

	assert (FPSCR.bit.fr == old_fr);
}

I (stsfpscr)
{
	CHECK_FP
	RN = FPSCR.val & 0x003FFFFF;
}

I (stsmfpscr)
{
	CHECK_FP
	RN -= 4;
	W32 (RN, FPSCR.val);
}

I (fsts)
{
	FN = FPUL;
}

I (flds)
{
	FPUL = FN;
}

I (ldsfpul)
{
	CHECK_FP
	FPUL = RN;
}

I (ldsmfpul)
{
	CHECK_FP
	FPUL = R32 (RN);
	RN += 4;
}

I (stsfpul)
{
	CHECK_FP
	RN = FPUL;
}

I (stsmfpul)
{
	CHECK_FP
	RN -= 4;
	W32 (RN, FPUL);
}

I (fldi0)
{
	CHECK_FP
	FN = 0.0f;
}

I (fldi1)
{
	CHECK_FP
	FN = 1.0f;
}

I (flt)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		FN = (float) (int) FPUL;
	} else {
		assert (_RN & 1);
		assert (0);
	}
}

I (fmov)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		FN = FM;
	} else {
		*(uint32_t *) &F(_RN+0) = *(uint32_t *) &F(_RM+0);
		*(uint32_t *) &F(_RN+1) = *(uint32_t *) &F(_RM+1);
	}
}

I (fmov_store)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		W32 (RN, *(uint32_t *) &FM);
	} else {
		W32 (RN+0, *(uint32_t *) &F(_RM));
		W32 (RN+4, *(uint32_t *) &F(_RM + 1));
	}
}

I (fmov_load)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		*(uint32_t *) &FN = R32 (RM);
	} else {
		*(uint32_t *) &F(_RN)   = R32 (RM+0);
		*(uint32_t *) &F(_RN+1) = R32 (RM+4);
	}
}

I (fmov_restore)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		*(uint32_t *) &FN = R32 (RM);
		RM += 4;
	} else {
		*(uint32_t *) &F(_RN)     = R32 (RM+0);
		*(uint32_t *) &F(_RN + 1) = R32 (RM+4);
		RM += 8;
	}
}

I (fmov_save)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		RN -= 4;
		W32 (RN, *(uint32_t *) &FM);
	} else {
		RN -= 8;
		W32 (RN+0, *(uint32_t *) &F(_RM));
		W32 (RN+4, *(uint32_t *) &F(_RM + 1));
	}
}

I (fmov_index_load)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		FN = R32 (R0 + RM);
	} else {
		*(uint32_t *) &F(_RN+0) = R32 (R0+RM+0);
		*(uint32_t *) &F(_RN+1) = R32 (R0+RM+4);
	}
}

I (fmov_index_store)
{
	CHECK_FP
	if (!FPSCR.bit.sz) {
		W32 (R0 + RN, *(uint32_t *) &FM);
	} else {
		W32 (R0 + RN + 0, *(uint32_t *) &F(_RM+0));
		W32 (R0 + RN + 4, *(uint32_t *) &F(_RM+1));
	}
}

I (fschg)
{
	assert (!FPSCR.bit.pr);

	FPSCR.bit.sz ^= 1;
}

I (fadd)
{
	assert (!FPSCR.bit.pr);
	assert (!FPSCR.bit.sz);

	FN += FM;
}

I (fsub)
{
	assert (!FPSCR.bit.pr);
	assert (!FPSCR.bit.sz);

	FN -= FM;
}

I (fmul)
{
	assert (!FPSCR.bit.pr);
	assert (!FPSCR.bit.sz);

	FN *= FM;
}

I (fdiv)
{
	assert (!FPSCR.bit.pr);
	assert (!FPSCR.bit.sz);

	FN /= FM;
}

I (ftrc)
{
	assert (!FPSCR.bit.pr);
	assert (!FPSCR.bit.sz);

	FPUL = (int32_t) FN;
}

static sh2_inst_desc *
decode (uint16_t inst)
{
	return inst_descs[DECODE_OP (inst)];
}

static int
translate_inst (sh2_jit *ctx)
{
	sh2_inst_desc *desc;
	uint16_t inst;

	inst = fetch (ctx->pc);

	desc = decode (inst);
	if (!desc) {
		goto fail;
	}

	if (desc->delayed) {
		uint32_t next;
		VK_ASSERT (!ctx->in_delay_slot);
		ctx->in_delay_slot = true;
		/* XXX fix PC for delay slot */
		sh2_jit_translate_inst (ctx);
		ctx->in_delay_slot = false;
		ctx->pc = next;
	}

	desc->translate (ctx, inst);
	ctx->pc += 2;

	return (desc->terminator) ? 1 : 0;
fail:
	return -1;
}

static int
translate_bb (sh2_jit *ctx)
{
	sh2_inst_desc *desc;
	vk_jit_bb *bb;
	int ret;

	/* Set the translation address */
	ctx->ta = ctx->pc;

	/* Make sure the BB is not already translated */
	VK_ASSERT (vk_jit_lookup_bb (ctx->ta) == NULL);

	/* Create a new BB */
	ctx->cur_bb = bb = vk_jit_new_bb (ctx->ta);
	VK_ASSERT (bb != NULL);

	vk_jit_bb_begin (ctx->cur_bb);

	/* Translate one instruciton at a time */
	do {
		ret = translate_inst (ctx);
		if (ret < 0) {
			goto fail;
		}
	} while (!ret);

	vk_jit_bb_finalize (bb);
	vk_jit_add_bb (bb);

	return 0;
fail:
	return -1;
}

int
sh2_jit_run (sh2 *ctx, int remaining)
{
	int ret;

	do {
		/* Fetch the BB from the LUT, or translate it if not found */
		ctx->cur_bb = vk_jit_lookup_bb (ctx->pc);
		if (!bb) {
			ret = translate_bb (ctx);
			VK_ASSERT (!!ret);
		}

		/* Run the basic block */
		/* XXX add a mapping from the ctx structure to offsets or a context descriptor in LLVM IL */
		ret = ctx->cur_bb->func (ctx);
		VK_ASSERT (!!ret);

		remaining -= ctx->elapsed;

	} while (remaining > 0);

	ctx->cur_bb = NULL;

	return cycles;
}

void
sh2_jit_delete (sh2_jit **_ctx)
{
	if (_ctx) {
		sh2_jit *ctx = *_ctx;
		vk_jit_free (ctx->
		FREE (_ctx);
	}
}

vk_cpu*
sh2_jit_new (void)
{
	sh2_jit *ctx;

	ctx = ALLOC (sh2_jit);
	if (!ctx) {
		goto fail;
	}

	ctx->jit = vk_jit_new ();
	if (!ctx->jit) {
		goto fail;
	}

	sh2_jit_init (ctx);

	return (vk_cpu *) ctx;
fail:
	sh2_jit_delete (&ctx);
	return NULL;
}

void
sh2_jit_init (sh2_jit *ctx)
{
	/* This associates a type and a value with R0-R15 */
	for (i = 0; i < 16; i++) {
		RVAL(i) = vk_jit_build_const_uint32 (JIT, 0);
	}

	ctx->base.run     = sh2_jit_run;
	ctx->base.reset   = sh2_jit_reset;
	ctx->base.set_irq = sh2_jit_set_irq;
	/* etc. */
}

/* GOALS:
 - LLVM-assisted JIT
 - Can use the interpreter as a fall-back (i.e., behaves as a threaded interpreter)
*/
