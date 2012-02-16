

typedef struct {
	union {
		uint32_t asid : 8;	/**< address space idenfifier */
		uint32_t      : 2;
		uint32_t vpn  : 22;	/**< virtual page number */
	} part;
	uint32_t full;
} ccn_pteh;

typedef struct {
	union {
		uint32_t wt  : 1;
		uint32_t sh  : 1;
		uint32_t d   : 1;
		uint32_t c   : 1;
		uint32_t sz1 : 1;
		uint32_t pr  : 2;
		uint32_t sz2 : 1;
		uint32_t v   : 1;
		uint32_t     : 1;
		uint32_t ppn : 19;
		uint32_t     : 3;
	} part;
	uint32_t full;
} ccn_ptel;

typedef struct {
	union {
		uint32_t sa : 3;
		uint32_t tc : 1;
		uint32_t    : 28;
	} part;
	uint32_t full;
} ccn_ptea;

typedef struct {
	union {
		uint32_t at   : 1; /**< address translation bit */
		uint32_t      : 1;
		uint32_t ti   : 1; /**< tlb invalidate */
		uint32_t      : 5;
		uint32_t sv   : 1; /**< single virtual mode bit */
		uint32_t sqmd : 1; /**< store queue mode bit */
		uint32_t urc  : 6; /**< utlb replace counter */
		uint32_t      : 2;
		uint32_t urb  : 6; /**< utlb replace boundary */
		uint32_t      : 2;
		uint32_t lrui : 6; /**< least recently used itlb */
	} part;
	uint32_t full;
} ccn_mmucr;

typedef struct {
	union {
		uint32_t oce : 1; /**< OC enable */
		uint32_t wt  : 1; /**< Write-thru enable */
		uint32_t cb  : 1; /**< Copy-back enable */
		uint32_t oci : 1; /**< OC invalidation */
		uint32_t     : 1;
		uint32_t ora : 1; /**< OC RAM enable */
		uint32_t     : 1;
		uint32_t oix : 1; /**< OC index enable */
		uint32_t ice : 1; /**< IC enable */
		uint32_t     : 2;
		uint32_t ici : 1; /**< IC invalidation */
		uint32_t     : 3;
		uint32_t iix : 1; /**< IC index enable */
		uint32_t     : 16;
	} part;
	uint32_t full;
} ccn_ccr;

#define PTEH	(*(ccn_pteh *) &IREG32(CCN_PTEH))
#define PTEL	(*(ccn_ptel *) &IREG32(CCN_PTEL))
#define PTEA	(*(ccn_ptea *) &IREG32(CCN_PTEA))
#define TTB	IREG32(CCN_TTB)
#define TEA	IREG32(CCN_TEA)
#define MMUCR	(*(ccn_mmucr *) &IREG32(CCN_MMUCR))
#define CCR	(*(ccn_ccr *) &IREG32(CCN_CCR))
#define QACR(n)	IREG32 (CCN_QACR0 + ((n) * 4))

/*
 * 4.6, "Store Queues"
 * 
 * 0xE0000000-0xE3FFFFFF are the SQ area. Bits [25:6] are the external
 * address bits. The actual SQ is selected through bit 5. Bits [4:2]
 * are ignored. Bits [1:0] must be clear.
 */

#define SQ_NUM(addr_) \
	(((addr_) >> 5) & 1)

#define SQ_ADDR(addr_) \
	((QACR (SQ_NUM (addr_) & 0x1C) << 24) | ((addr_) & 0x03FFFFFF))

static void
sync_sq (sh4 *ctx, uint32_t addr)
{
	if (IS_STORE_QUEUE (addr)) {
		unsigned sq_num = SQ_NUM (addr);
		uint32_t sq_addr = SQ_ADDR (addr);
		VK_ABORT ("unimplemented");
	}
}

static uint32_t
sh4_read_sq (sh4 *ctx, unsigned size, uint32_t addr)
{
	if (MMUCR.part.at == 1 || MMUCR.part.sqmd == 1) {
		return ctx->sq[SQ_NUM (addr)][addr / 4];
	}
	/* address error */
	return 0;
}

static void
sh4_write_sq (sh4 *ctx, unsigned size, uint32_t addr, uint32_t val)
{
	if (MMUCR.part.at == 1 || MMUCR.part.sqmd == 1) {
		ctx->sq[SQ_NUM (addr)][addr / 4] = val;
	}
	/* address error */
}

/* On-Chip Area
 * ------------
 *
 *  0xE000 0000 SW
 *  0xE400 0000 reserved
 *  0xF000 0000 icache addresses
 *  0xF100 0000 icache data
 *  0xF200 0000 itlb addresses
 *  0xF300 0000 itlb data 1&2
 *  0xF400 0000 dcache addresses
 *  0xF500 0000 dcache data
 *  0xF600 0000 utlb addresses
 *  0xF700 0000 utbl data 1&2
 *  0xF800 0000 reserved
 *  0xFF00 0000 iregs
 */

static void
check_size_and_alignment (sh4 *ctx, unsigned size, uint32_t addr)
{
	vk_cpu *cpu = (vk_cpu *) ctx;

	/* As long as the code uses the Rx and Wx macros, there's no need to
	 * check size here. */

	switch (size) {
	case 2:
		VK_CPU_ASSERT (cpu, !(addr & 1));
		break;
	case 4:
		VK_CPU_ASSERT (cpu, !(addr & 3));
		break;
	case 8:
		VK_CPU_ASSERT (cpu, !(addr & 7));
		break;
	}
}

/* From 3.3.1, "Physical Memory Space"
 *
 *  Privileged Mode                             User Mode
 *  ---------------                             ---------
 *  0x00000000 P0 cache,translated		0x00000000 U0 cache,translated
 *  0x80000000 P1 cache,untranslated		0x80000000
 *  0xA0000000 P2 non-cache,untranslated
 *  0xC0000000 P3 cache,translated
 *  0xE0000000 P4 non-cache,untranslated	0xE0000000 store queue area,translated (if SQMD == 0) or error
 */

/*
 * External Memory Map
 * -------------------
 *
 *  0x0000 0000 area 0
 *  0x0400 0000 area 1
 *  ...
 *  0x1800 0000 area 6
 *  0x1C00 0000 reserved area 7 if no MMU, IREG if through MMU
 */

/*
 * From 3.3.5, "Address Translation"
 *
 * "... the ITLB is used for instruction accesses and the UTLB for data
 *  accesses. In the event of an access to an area other than the P4 area,
 *  the accessed virtual address is translated to a physical address. If	P4 has no corresponding 29-bit physical address
 *  the virtual address belongs to the P1 or P2 area, the physical address
 *  is uniquely determined without accessing the TLB. If the virtual address	P1,P2 are always physical
 *  belongs to the P0, U0, or P3 area, the TLB is searched using the virtual	P0,P3,U0 are always virtual
 *  address, and if the virtual address is recorded in the TLB, a TLB hit is
 *  made and the corresponding physical address is read from the TLB. If the
 *  accessed virtual address is not recorded in the TLB, a TLB miss exception
 *  is generated and processing switches to the TLB miss exception routine."
 */

#if 0

static void
mmu_virt_to_phys_1K (uint32_t addr)
{
	uint32_t vpn  = addr & 0xFFFFFC00;
	uint32_t offs = addr & 0x000003FF;

	find urb entry with vpn
	raise utlb exception if none
	get ppn
	compose address or
}

static void
mmu_virt_to_phys_4K (uint32_t addr)
{
	uint32_t vpn  = addr & 0xFFFFF000;
	uint32_t offs = addr & 0x00000FFF;
}

static void
mmu_virt_to_phys_64K (uint32_t addr)
{
	uint32_t vpn  = addr & 0xFFFF0000;
	uint32_t offs = addr & 0x0000FFFF;
}

static void
mmu_virt_to_phys_1M (uint32_t addr)
{
	uint32_t vpn  = addr & 0xFFF00000;
	uint32_t offs = addr & 0x000FFFFF;
}

static uint32_t (* mmu_virt_to_phys[4])(uint32_t addr) = {
	mmu_virt_to_phys_1K,
	mmu_virt_to_phys_4K,
	mmu_virt_to_phys_64K,
	mmu_virt_to_phys_1M,
};

static void
mmu_translate_inst (sh4 *ctx, uint32_t va, uint32_t *pa)
{
	if (P4) {
		/* address error */
	} else if (P2) {
		/* no translation, no cache */
	} else if (P1) {
		if (CCR.part.ice == 0) {
			/* no translation, no cache */
		} else {
			/* no translation, cache */
		}
	} else if (P0 || P3 || U0) {
		if (!MMUCR.part.at) {
			if (CCR.part.ice == 0) {
				/* no translation, no cache */
			} else {
				/* no translation, cache */
			}
		} else {
			cond = (SH == 0 && (MMUCR.part.SV == 0 || SR.MD == 0)) ?
			       (VPN match and ASID match and V = 1) :
			       (VPN match and V = 1);
			if (!cond) {
				if (no match in UTLB) {
					/* ITLB miss */
				} else {
					/* record in ITLB */
				}
			} else if (multiple hits) {
				/* ITLB multiple hit */
			} else {
				if (SR.MD == 0 && !PR) {
					/* ITLB protection violation */
				}
				if (C & CCR.ICE == 1) {
					/* cache access */
				} else {
					/* memory access */
				}
			}
		}
	}
}

#define IS_P4(addr_) \
	((addr_) >= 0xE0000000)

#define IS_U0(addr_) \
	((addr_) < 0x80000000)

static uint32_t
va_to_vpn (sh4 *ctx, uint32_t va)
{
}

static const uint32_t vpn_mask[4] = {
	0xFFFFFC00, 0xFFFFF000, 0xFFFF0000, 0xFFF00000
};

static bool
tlb_entry_matches_address (sh4 *ctx, int i, uint32_t sz, uint32_t vaddr)
{
	uint32_t asid, vpn;
	bool same_vpn, same_asid;

	vpn  = vpn_mask[sz] & vaddr;
	asid = PTEH.part.asid;

	same_vpn  = vpn == ctx->utlb[i].vpn;
	same_asid = !CHECK_ASID || (asid == ctx->utlb[i].asid);

	return same_vpn && same_asid;
}

static int
lookup_utlb (sh4 *ctx, uint32_t addr, unsigned *num_hits)
{
	int i;
	for (i = 0; i < NUM_UTLB_ENTRIES; i++) {
		if (tlb_entry_matches_address (ctx, i, addr))
			*num_hits ++;
	}
	return i;
}

static int
lookup_itlb (vk_sh *ctx, uint32_t addr, unsigned *num_hits)
{
	int i
	for (i = 0; i < NUM_ITLB_ENTRIES; i++) {
		if (tlb_entry_matches_address (ctx, i, addr))
			*num_hits ++;
	}
	return i;
}

static int
lookup_utlb_with_exceptions (sh4 *ctx, uint32_t addr, unsigned *num_hits)
{
	int entry = lookup_dtlb (ctx, addr, num_hits);
	if (*num_matches == 0) {
		/* No matches */
		raise_dtlb_miss (ctx, addr);
		return -1;
	} else if (*num_maches > 1) {
		/* Multiple matches */
		raise_dtlb_multiple_hits (ctx, addr);
		return -1;
	}
	return entry;
}

with_exceptions (sh4 *ctx, uint32_t addr, 
{
	int i;
	for (i = 0; i < 16; i++) {
		if (itbl_entry_matches_address (&ctx->itlb[i], addr))
			*num++;
	}
	if (*inum == 0) {
		/* No ITLB entry, lookup the DTLB */
		unsigned unum;
		int 
		lookup_utlb (ctx, addr, &unum);
		if (unum == 0) {
			/* No DTLB entry, raise exception */
			raise_itbl_miss_exception (ctx, addr);
			return NULL;
		}
		if (unum > 1) {
			/* Multiple DTLB entries, raise exception */
			raise_itlb_multiple_hit_exception (ctx, addr);
			return NULL;
		}
		/* Unique match, transfer the entry to the ITBL */
		itlb_add_utlb_entry (

	if (*inum > 1) {
		return NULL;
	}
	if (*num == 0) {
		lookup_utlb (ctx, addr, &unum
}

static void
mmu_translate_data (sh4 *ctx, uint32_t va, uint32_t *pa, bool read)
{
	if (P4) {
		/* on-chip, no translation, no cache */
	} else if (P2) {
		/* no translation, no cache */
	} else if (P1) {
		if (CCR.part.oce == 0) {
			/* no translation, no cache */
		} else if (CCR.part.cb == 0) {
			/* no translation, cache write-thru */
		} else {
			/* no translation, cache copy-back */
		}
	} else if (P0 || P3 || U0) {
		if (MMUCR.part.at == 0) {
			if (CCR.part.wt == 0) {
				/* no translation, cache copy-back */
			} else {
				/* no translation, cache write-thru */
			}
		} else {
			cond = (SH == 0 && MMUCR.part.sv == 0 || SRC.MD == 0) ?
			        (VPNs match and V = 1 and ASID match) :
			        (VPNs match and V = 1);
			if (zero matches) {
				/* data TLB miss */
			} else if (multiple matches) {
				/* data TLB multiple hit */
			} else {
				/* we got a unique hit; parse the page */

				


				if (C & CCR.part.oce == 1) {
					if (WT) {
						/* cache copy-back */
					} else {
						/* cache write-thru */
					}
				} else {
					/* no cache */
				}
			}
		}
	}
}

static void
get_cte_for_addr (sh4 *ctx, uint32_t addr, bool *c, bool *t, bool *e)
{
	/* 3.3.3, "Virtual Memory Space" */
	*c = *t = *e = false;
	if (IS_PRIVILEGED) {
		if (addr < 0x7C000000 ||
		    (addr >= 0xC0000000 && addr <= 0xDFFFFFFF)) {
			/* P0 and P3 */
			*t = true;
			*c = true;
		} else if (addr >= 0x80000000 && addr <= 0x9FFFFFFF) {
			/* P1 */
			*c = true;
		}
	} else {
		if (addr < 0x80000000) {
			/* U0 */
			*c = true;
			*t = true;
		} else if (IS_STORE_QUEUE (addr)) {
			*t = true;
		}
	}
}

#endif
