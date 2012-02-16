
typedef struct {
	uint16_t tpsc : 3; /* Timer Frequency */
	uint16_t ckeg : 2; /* Clock Edge */
	uint16_t unie : 1; /* Underflow Interrupt Control */
	uint16_t icpe : 2; /* Input Capture Control [Channel 2 only] */
	uint16_t unf  : 1; /* TCNT Underflow */
	uint16_t icpf : 1; /* Input Capture [Channel 2 only] */
	uint16_t : 6;
} tmu_tcr;

#define TOCR		IREG8(TMU_TOCR)
#define TSTR		IREG8(TMU_TSTR)
#define TCOR(n_)	IREG32(TMU_TCOR0 + (n_) * 0x10)
#define TCNT(n_)	IREG32(TMU_TCNT0 + (n_) * 0x10)
#define TTCR(n_)	(*(tmu_tcr *) &IREG32(TMU_TCR0 + (n_) * 0x10))

/* TODO: external clock */
/* TODO: frequency scaling */

static void
tmu_tick_channel (sh4 *ctx, unsigned ch)
{
	uint32_t old, new;

	old = TCNT(ch);
	new = old - 1;

	TCNT(ch) = new;
	if ((old ^ new) >> 31) {
		TCNT(ch) = TCOR(ch);
		TTCR(ch).unf = 1;
		if (TTCR(ch).unie) {
			// IRQ
			assert (0);
		}
	}
}

static void
tmu_tick (sh4 *ctx)
{
	if (TSTR & 1) {
		tmu_tick_channel (ctx, 0);
	}
	if (TSTR & 2) {
		tmu_tick_channel (ctx, 1);
	}
	if (TSTR & 4) {
		tmu_tick_channel (ctx, 2);
	}
}
