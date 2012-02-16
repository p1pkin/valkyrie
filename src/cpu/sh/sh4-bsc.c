
#define RFCR	IREG16(BSC_RFCR)

void
bsc_tick (sh4 *ctx)
{
	RFCR = RFCR + 1;
}
