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

/* 
 * SEGA Hikaru
 *
 * Specs
 * =====
 *
 * Taken from system16.com
 *
 * CPU			2 x Hitachi SH-4 @ 200 MHz
 * Graphic Engine	Sega Custom 3D 
 * Sound Engine		2 x ARM7 Yamaha AICA @ 45 MHz + ARM7, 64 channel ADPCM
 * Main Memory		64 MB [32 for master, 32 for slave]
 * Graphic Memory	28 MB [8 FB, 4 CMDRAM, 4+4 TEXRAM, 8 UNKNOWN]
 * Sound Memory		8 MB [per AICA board]
 * Media		ROM Board (max 352 MB)
 * Colors		24bit
 * Resolution		24 KHz 496x384, 31 KHz 640x480 
 * Shading		Flat, Linear, Phong
 * Lighting		Horizontal, Spot
 *			1024 lights per scene [= total LIGHTS]
 *			4 lights per polygon [= 1 LIGHTSET, 4 LIGHTS]
 *			8 window surfaces [viewports]. 
 * Effects		Fog, Depth Queueing, Stencil, Shadow, Motion blur
 * Others Capabilties	Bitmap Layer x 2
 *			Calendar [Note: AICA RTC]
 *			Dual Monitor (24 kHz)
 * Extensions		communication, 4 channel audio, PCI, MIDI, RS-232C 
 * Connection		Jamma Video compliant
 *
 *
 * ICs
 * ===
 *
 * According to the RAM test:
 *
 * 15,16,17S,18S = @0C000000 RAM (32 MB), Master RAM
 * 22,23,24S,25S = @16000000 RAM (32 MB), Slave RAM
 * 38 39S        = @14000000 RAM (4 MB), CMDRAM
 * 41            = @16000000 RAM (4 MB), Unknown, GPU-related
 * 42            = @16000000 RAM (4 MB), Unknown, GPU-related
 * 44,45S,46,47S = @1B000000 RAM (8 MB), TEXRAM
 * 91S,92S       = @0C000000 CMOS SRAM (64 KB), Backup RAM
 * 98            = @02800000 8 MB SDRAM (main AICA board)
 *
 * Not tested by the bootrom:
 *
 * 33,34S,35,36S = 8 MB; is it the framebuffer?
 *
 *
 * BIOSes
 * ======
 *
 * Currently three different bios revisions are known: 0.84, 0.92, and 0.96
 * (see the MAME driver.)
 *
 * Any IRQ controller in there pumping stuff from the various devices to
 * the master SH-4 IRL pins and Port A?
 *
 * The master SH-4 is configured for external-request DMAC (DTD). Requests
 * are sent either to the master SH-4 or the slave SH-4.
 *
 * TMU Channel 2 is configured for input capture; the DMAC is automatically
 * activeted whenever the interrupt fires. Timed DMA, way to go!
 *
 *
 * EEPROMs
 * =======
 *
 * The Hikaru hosts a few EEPROMs:
 *  - One on the MAINBD, connected to the master SH4 Port A.
 *  - One on the ROMBD, which likely holds game-specific data and protection.
 *  - One on the INPTBD, at 0800000[AC], unknown usage.
 *
 *
 * MAINBD EEPROM
 * =============
 *
 * The MAINBD EEPROM is a serial 128 x 8-bit EEPROM. It is interfaced to bits
 * 2-5 of PDTRA. The port is configured as follows:
 *
 *  PCTRA = (PCTRA & ~FF0) | 950
 *  PDTRA = PDTRA & ~3C
 *
 * See @0C00BF5C.
 *
 *  bit 2	Output		[configured as: pulled up, output]
 *  bit 3	Clock		[configured as: pulled up, output]
 *  bit 4	Chip Select	[configured as: pulled up, output]
 *  bit 5	Input		[configured as: not pulled up, input]
 *
 * See @0C0067AC, @0C0067DC.
 *
 *
 * ROMBD EEPROM, Type 1
 * ====================
 *
 * Located at 03000000, in bank_base + 0x14. It's a serial EEPROM of size
 * 128 x 8-bit entries. An 9C346. Bits (active low):
 *
 *  bit 0	Output
 *  bit 1	Clock
 *  bit 2	Chip Select
 *  bit 3	Input
 *
 * See @0C00C27E, @0C00C2AE
 *
 *
 * ROMBD EEPROM, Type 2
 * ====================
 *
 * An 76X100 secure EEPROM.
 *
 * MIE EEPROM
 * ==========
 *
 * See hikaru-mie.c
 */

#include "vk/core.h"
#include "vk/games.h"

#include "cpu/sh/sh4.h"

#include "mach/hikaru/hikaru.h"
#include "mach/hikaru/hikaru-memctl.h"
#include "mach/hikaru/hikaru-mscomm.h"
#include "mach/hikaru/hikaru-mie.h"
#include "mach/hikaru/hikaru-aica.h"
#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-renderer.h"

/*
 * Port A
 * ======
 *
 * The master SH-4 port A is used for a variety of things. The bitmask is
 * as follows:
 *
 * [M] xxxx xxii iiee eexM
 *
 * x = Unused
 * i = IRQ causes
 * e = MAINBD EEPROM
 * M = master-to-slave communication
 *
 * Note: apparently bit M is connected to the slave NMI pin; writing a
 * specific bit pattern there requestes an NMI to the slave SH-4.
 *
 * [S] xxxx xxxx xxxx xxSx
 *
 * S = slave-to-master communication?
 *
 * Note: GPIOIC is never access by the bootrom.
 */

/* These functions get called by the SH-4 core */

static int
porta_get_m (sh4_t *ctx, uint16_t *val)
{
	hikaru_t *hikaru = (hikaru_t *) ctx->base.mach;
	*val = hikaru->porta_m;
	return 0;
}

static int
porta_get_s (sh4_t *ctx, uint16_t *val)
{
	hikaru_t *hikaru = (hikaru_t *) ctx->base.mach;
	*val = hikaru->porta_s;
	return 0;
}

/* XXX check port A in the core; wrong values reported (0 instead of 1)? */

static int
porta_put_m (sh4_t *ctx, uint16_t val)
{
	hikaru_t *hikaru = (hikaru_t *) ctx->base.mach;
	hikaru->porta_m = val;

	/* This is a simple hack to detect three consecutive cycles of
	 * low-high-low pin; the pattern 000-111-000[0000] is there because
	 * in the SH-4:
	 *
	 * "A noise-cancellation feature is built in, and the IRL interrupt
	 *  is not detected unless the levels sampled at every bus clock
	 *  cycle remain unchanged for three consecutive cycles, so that no
	 *  transient level on the IRL pin change is detected."
	 *
	 * The additional [0000] bits can be explained with:
	 *
	 *  "[...] the NMI interrupt is not detected for a maximum of 6 bus
	 *   clock cycles after the modification."
	 *
	 * Basically it's flipping the NMI pin 0->1->0.
	 *
	 * See sections 19.2.1 and 19.2.2 of the SH-4 manual.
	 */
	/* XXX ideally this should be done automatically by the SH-4 core;
	 * but it requires a rewrite of the IRQ code based on lines */
	hikaru->porta_m_bit0_buffer = (hikaru->porta_m_bit0_buffer << 1) |
	                              (val & 1);
	if ((hikaru->porta_m_bit0_buffer & 0x1FFF) == 0x1C7F) {
		/* Send an IRQ to the slave */
		VK_CPU_LOG (ctx, " ### PORTA: sending NMI to SLAVE!");
		vk_cpu_set_irq_state (hikaru->sh_s, SH4_IESOURCE_NMI, VK_IRQ_STATE_RAISED);
		hikaru->porta_m_bit0_buffer = 0;
	}
	return 0;
}

static int
porta_put_s (sh4_t *ctx, uint16_t val)
{
	hikaru_t *hikaru = (hikaru_t *) ctx->base.mach;
	hikaru->porta_s = val;
	hikaru->porta_s_bit1_buffer = (hikaru->porta_s_bit1_buffer << 1) |
	                              ((val >> 1) & 1);
	/* TODO */
	return 0;
}

/*
 * Unknown MMIOs (Master)
 * ======================
 *
 * All of these seem connected in some way. All of them act as semaphores
 * of some kind, and are definitely related to the GPU.
 *
 * 00400000  RW 16-bit
 *
 * 01000000  RW 16-bit	
 * 01000006   W 16-bit	See PH:@0C012752.
 *
 * 01000100  RW 16-bit	
 */

static int
unk_m_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	uint16_t *val16 = (uint16_t *) val;

	VK_ASSERT (size == 2);

	switch (addr) {
	case 0x01000000:
		*val16 = hikaru->unk01000000_m;
		break;
	case 0x01000100:
		*val16 = hikaru->unk01000100_m;
		break;
	default:
		return -1;
	}
	return 0;
}

static int
unk_m_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_t *hikaru = (hikaru_t *) dev->mach;

	VK_ASSERT (size == 2);

	switch (addr) {
	case 0x01000000:
		hikaru->unk01000000_m = val;
		break;
	case 0x01000100:
		hikaru->unk01000100_m = val;
		break;
	default:
		return -1;
	}
	return 0;
}

/* XXX hack */
static vk_device_t unk_m = {
	0,
	.get = unk_m_get,
	.put = unk_m_put
};

/* Unknown devices in the slave address space */

static int
unk_s_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	hikaru_t *hikaru = (hikaru_t *) dev->mach;
	uint16_t *val16 = (uint16_t *) val;
	uint32_t *val32 = (uint32_t *) val;

	switch (addr) {
	case 0x14000800:
		/* Controlled by 04000010 and 0400001C */
		VK_ASSERT (size == 4);
		*val32 = 0x17C311DB; /* SEGA PCI ID #2 */
		break;
	case 0x1A800008:
		VK_ASSERT (size == 2);
		*val16 = hikaru->unk1A800008_s;
		break;
	case 0x1B000100:
		VK_ASSERT (size == 2);
		*val16 = hikaru->unk1B000100_s;
		break;
	default:
		return -1;
	}
	return 0;
}

static int
unk_s_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	hikaru_t *hikaru = (hikaru_t *) dev->mach;

	switch (addr) {
	case 0x1400080D:
		/* Controlled by 04000010 and 0400001C */
		VK_ASSERT (size == 1);
		break;
	case 0x14000804:
	case 0x14000810:
	case 0x14000814:
	case 0x14000818:
	case 0x1400081C:
		/* Controlled by 04000010 and 0400001C */
		VK_ASSERT (size == 4);
		break;
	case 0x1A800008:
		VK_ASSERT (size == 2);
		hikaru->unk1A800008_s = val;
		break;
	case 0x1B000100:
		/* This is a semaphore, akin to 01000000 and 01000100 in
		 * the master address space. */
		VK_ASSERT (size == 2);
		hikaru->unk1B000100_s = val ^ 0x100;
		break;
	default:
		return -1;
	}
	return 0;
}

/* XXX hack */
static vk_device_t unk_s = {
	0,
	.get = unk_s_get,
	.put = unk_s_put
};

/*
 * IRQs
 * ====
 *
 * The master SH-4 is configured for independent per-pin external IRQs. It
 * looks like that the IRQ sources are as follows:
 *
 *  IRL0 unused/unhandled
 *  IRL1 GPU hardware, 
 *  IRL2 memory controller DMA termination
 *  IRL3 unused/unhandled
 *
 * IRQ-related Registers
 * =====================
 *
 * The following registers are used in the IRL1 handling routine to check
 * what happened, see @0C00174C.
 *
 * PDTRA (active low):
 *
 *  0040	IRQ source is GPU
 *  0080	IRQ source is AICA/DMA
 *  0100	IRQ source is UNKNOWN [slave?], calls @0C000A30
 *  0200	Error
 *
 * For more informations on the GPU IRQs, see hikaru-gpu.c
 */

static void
hikaru_raise_irq (vk_machine_t *mach, unsigned num, uint16_t porta)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	vk_cpu_set_irq_state (hikaru->sh_m, num, VK_IRQ_STATE_RAISED);
	hikaru->porta_m &= ~porta;
}

void
hikaru_raise_gpu_irq (vk_machine_t *mach)
{
	hikaru_raise_irq (mach, SH4_IESOURCE_IRL2, 0x40);
}

void
hikaru_raise_aica_irq (vk_machine_t *mach)
{
	hikaru_raise_irq (mach, SH4_IESOURCE_IRL2, 0x80);
}

void
hikaru_raise_memctl_irq (vk_machine_t *mach)
{
	hikaru_raise_irq (mach, SH4_IESOURCE_IRL1, 0);
}

/* XXX this should really be 200 MHz; downclocked to 50 MHz to make debugging
 * a little more bearable. */
static const unsigned cycles_per_line = (50 * MHZ) / (60 * 480);

static void
hikaru_run_cycles (vk_machine_t *mach, int cycles)
{
	hikaru_t *hikaru = (hikaru_t *) mach;

	/* Run the master */
	hikaru->sh_current = hikaru->sh_m;
	vk_cpu_run (hikaru->sh_m, cycles);

	/* Run the slave */
	hikaru->sh_current = hikaru->sh_s;
	vk_cpu_run (hikaru->sh_s, cycles);

	/* Run the MEMCTL and GPU */
	vk_device_exec (hikaru->memctl_m, cycles);
	/* XXX run the slave MEMCTL? I've never seen it used, and would
	 * like to debug it a bit before enabling it. */
	vk_device_exec (hikaru->gpu, cycles);
}

static int
hikaru_run_frame (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	unsigned line;

	/* XXX move the main loop into vk_machine */

	VK_LOG (" *** VBLANK-OUT %s ***", vk_machine_get_debug_string (mach));

	for (line = 0; line < 480; line++) {
		hikaru_run_cycles (mach, cycles_per_line);
		hikaru_gpu_hblank_in (hikaru->gpu, line);
	}

	VK_LOG (" *** VBLANK-IN  %s ***", vk_machine_get_debug_string (mach));
	hikaru_gpu_vblank_in (hikaru->gpu);

	for (line = 480; line < (480+64); line++) {
		hikaru_run_cycles (mach, cycles_per_line);
		hikaru_gpu_hblank_in (hikaru->gpu, line);
	}

	/* this may actually be an hblank-out IRQ */
	hikaru_gpu_vblank_out (hikaru->gpu);

	/* XXX AICA */
	hikaru_raise_irq (mach, SH4_IESOURCE_IRL2, 0x80);

	return 0;
}

static void
hikaru_reset (vk_machine_t *mach, vk_reset_type_t type)
{
	hikaru_t *hikaru = (hikaru_t *) mach;

	vk_buffer_clear (hikaru->ram_m);
	vk_buffer_clear (hikaru->ram_s);
	vk_buffer_clear (hikaru->cmdram);
	vk_buffer_clear (hikaru->texram[0]);
	vk_buffer_clear (hikaru->texram[1]);
	vk_buffer_clear (hikaru->fb);
	vk_buffer_clear (hikaru->aica_ram_m);
	vk_buffer_clear (hikaru->aica_ram_s);
	vk_buffer_clear (hikaru->mie_ram);
	vk_buffer_clear (hikaru->bram);

	/* XXX load bram from file */

	vk_cpu_reset (hikaru->sh_m, type);
	vk_cpu_reset (hikaru->sh_s, type);

	vk_cpu_set_state (hikaru->sh_s, VK_CPU_STATE_RUN);

	/* Port A's are active low */
	hikaru->porta_m = 0xFFFF;
	hikaru->porta_s = 0xFFFF;

	vk_device_reset (hikaru->memctl_m, type);
	vk_device_reset (hikaru->memctl_s, type);
	vk_device_reset (hikaru->mscomm, type);
	vk_device_reset (hikaru->mie, type);
	vk_device_reset (hikaru->aica_m, type);
	vk_device_reset (hikaru->aica_s, type);
	vk_device_reset (hikaru->gpu, type);

	hikaru->unk01000000_m = 0;
	hikaru->unk01000100_m = 0;
	hikaru->unk1A800008_s = 0xFFFF;
	hikaru->unk1B000100_s = 0xFEFF;

	vk_renderer_reset (hikaru->base.renderer);
}

static const char *
hikaru_get_debug_string (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	static char out[256];
	char *mstr, *sstr;
	const char *gpustr;

	mstr = strdup (vk_cpu_get_debug_string (hikaru->sh_m));
	sstr = strdup (vk_cpu_get_debug_string (hikaru->sh_s));
	gpustr = hikaru_gpu_get_debug_str (hikaru->gpu);

	sprintf (out, "[%s %04X] [%s %04X] [%s]",
	         mstr, hikaru->porta_m,
	         sstr, hikaru->porta_s,
	         gpustr);

	free (mstr);
	free (sstr);
	return out;
}

static int
hikaru_load_state (vk_machine_t *mach, FILE *fp)
{
	vk_machine_reset (mach, VK_RESET_TYPE_HARD);
	/* XXX now load each component (including the CPUs and the unknown HW) */
	return -1;
}

static int
hikaru_save_state (vk_machine_t *mach, FILE *fp)
{
	return -1;
}

static void
hikaru_dump (vk_machine_t *mach)
{
	hikaru_t *hikaru = (hikaru_t *) mach;
	char *name = mach->game->name;

	if (!name)
		return;

	vk_buffer_dumpf (hikaru->ram_m,		"%s-ram-m.bin", name);
	vk_buffer_dumpf (hikaru->ram_s,		"%s-ram-s.bin", name);
	vk_buffer_dumpf (hikaru->cmdram,	"%s-cmdram.bin", name);
	vk_buffer_dumpf (hikaru->texram[0],	"%s-texram-0.bin", name);
	vk_buffer_dumpf (hikaru->texram[1],	"%s-texram-1.bin", name);
	vk_buffer_dumpf (hikaru->fb,		"%s-fb.bin", name);
	vk_buffer_dumpf (hikaru->aica_ram_m,	"%s-aica-m.bin", name);
	vk_buffer_dumpf (hikaru->aica_ram_s,	"%s-aica-s.bin", name);
	vk_buffer_dumpf (hikaru->bram,		"%s-bram.bin", name);
	vk_buffer_dumpf (hikaru->mie_ram,	"%s-mie.bin", name);
}

/*
 * Master SH-4 Memory Map
 * ======================
 *
 * Area 0	00000000-00200000 Boot ROM 
 *		00400000-00400003 ?
 *		00800000-0083FFFF On-board Switches + MIE
 *		00C00000-00C0FFFF Backup RAM
 *		01000000-01000003 ?
 *		01000100-01000103 ?
 *		02000000-02FFFFFF Aperture-02
 *		03000000-03FFFFFF Aperture-03
 * Area 1	04000000-0400003F Memory Controller [Master]
 * Area 3	0C000000-0DFFFFFF RAM
 * Area 5	14000000-140000FF Master/Slave Communication Box
 *		14000100-143FFFFF Command RAM
 *		15000000-150FFFFF Geometry Processor
 *		16000000-16FFFFFF Aperture-16
 *		17000000-17FFFFFF Aperture-17
 * Area 6	18001000-180010FF GPU Regs
 *		1A000000-1A0FFFFF Image Generator
 *		1B000000-1B7FFFFF Frame Buffer
 */

static vk_mmap_t *
setup_master_mmap (hikaru_t *hikaru)
{
	vk_mmap_t *mmap = vk_mmap_new (&hikaru->base);
	vk_region_t *region;

	if (!mmap)
		return NULL;

	region = vk_region_ram_new (0x0C000000, 0x0DFFFFFF, 0x01FFFFFF, 0,
	                            hikaru->ram_m, "RAM/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_rom_new (0x00000000, 0x001FFFFF, 0x1FFFFF, 0,
	                            hikaru->bootrom, "BOOTROM/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x00400000, 0x00400001, 1,
	                            VK_REGION_RW | VK_REGION_SIZE_16 | VK_REGION_SIZE_32 | VK_REGION_LOG_RW,
	                            hikaru->gpu, "UNK/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x00800000, 0x0083FFFF, 0x3FFFF,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             hikaru->mie, "MIE/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_ram_new (0x00C00000, 0x00C0FFFF, 0xFFFF, 0,
	                            hikaru->bram, "BRAM/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_nop_new (0x01000000, 0x010001FF, 0x1FF,
	                            VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                            "UNK/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x02000000, 0x03FFFFFF, 0x01FFFFFF,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL,
	                             hikaru->memctl_m, "APERTURE02/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x04000000, 0x0400003F, 0x3F,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             hikaru->memctl_m, "MEMCTL/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x14000000, 0x1400002F, 0x3F,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             hikaru->mscomm, "MSCOMM/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_ram_new (0x14000030, 0x143FFFFF, 0x3FFFFF, 0,
	                            hikaru->cmdram, "CMDRAM/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x15000000, 0x150FFFFF, 0x0FFFFF,
	                             VK_REGION_RW | VK_REGION_SIZE_16 | VK_REGION_SIZE_32 | VK_REGION_LOG_RW,
	                             hikaru->gpu, "GPU/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x16000000, 0x17FFFFFF, 0x01FFFFFF,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL,
	                             hikaru->memctl_m, "APERTURE16/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x18001000, 0x1800101F, 0x1F,
	                             VK_REGION_RW | VK_REGION_SIZE_32 | VK_REGION_LOG_RW,
	                             hikaru->gpu, "GPU/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x1A000000, 0x1A0FFFFF, 0x0FFFFF,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             hikaru->gpu, "GPU/M");
	vk_mmap_add_region (mmap, region);

	region = vk_region_ram_new (0x1B000000, 0x1B7FFFFF, 0x7FFFFF, 0,
	                            hikaru->fb, "TEXRAM/M");
	vk_mmap_add_region (mmap, region);

	return mmap;
}

/*
 * Slave SH-4 Memory Map
 * =====================
 *
 * Area 0	00000000-001FFFFF Boot ROM
 * Area 1	04000000-0400003F Memory Controller [Slave]
 * Area 3	0C000000-0DFFFFFF RAM
 * Area 4	10000000-100000FF Master/Slave Communication Box
 *		10000100-103FFFFF Command RAM [Slave]
 * Area 5	14000800-1400083F Master's 18000000
 * Area 6	1A800000-1A800003 GPU
 *		1B000100-1B000103 GPU
 */

static vk_mmap_t *
setup_slave_mmap (hikaru_t *hikaru)
{
	vk_mmap_t *mmap = vk_mmap_new (&hikaru->base);
	vk_region_t *region;

	if (!mmap)
		return NULL;

	region = vk_region_ram_new (0x0C000000, 0x0DFFFFFF, 0x01FFFFFF, 0,
	                            hikaru->ram_s, "RAM/S");
	vk_mmap_add_region (mmap, region);

	region = vk_region_rom_new (0x00000000, 0x001FFFFF, 0x001FFFFF, 0,
	                            hikaru->bootrom, "BOOTROM/S");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x04000000, 0x0400003F, 0x3F,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
                                     hikaru->memctl_s, "MEMCTL/S");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x10000000, 0x1000003F, 0x3F,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             hikaru->mscomm, "MSCOMM/S");
	vk_mmap_add_region (mmap, region);

	region = vk_region_ram_new (0x10000100, 0x103FFFFF, 0x3FFFFF, VK_REGION_LOG_WRITE,
	                            hikaru->cmdram, "CMDRAM/S");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x14000800, 0x1400083F, 0x3F,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             &unk_s, "UNK/S");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x1A800000, 0x1A8000FF, 0xFF,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             &unk_s, "UNK/S");
	vk_mmap_add_region (mmap, region);

	region = vk_region_mmio_new (0x1B000100, 0x1B0001FF, 0xFF,
	                             VK_REGION_RW | VK_REGION_SIZE_ALL | VK_REGION_LOG_RW,
	                             &unk_s, "UNK/S");
	vk_mmap_add_region (mmap, region);

	return mmap;
}

static int
hikaru_set_rombd_config (hikaru_t *hikaru)
{
	vk_game_t *game = hikaru->base.game;
	hikaru_rombd_config_t *config = &hikaru->rombd_config;
	uint32_t rombd_offs = 0, eprom_bank_size = 0, maskrom_bank_size = 0;
	bool has_rom = true, maskrom_is_stretched = false;

	/* The fields computed here are used in hikaru-memctl.c::rombd_get () */

	/* We require at least the bootrom to be loaded */
	/* XXX add a "required" field to the json file */
	if (!hikaru->bootrom)
		return -1;

	/* Determine the layout of ROMBD data within the external bus; see
	 * hikaru-memctl.c for more details. */
	if (!strcmp (game->name, "bootrom")) {
		has_rom = false;
		hikaru->eprom = NULL;
		hikaru->maskrom = NULL;
	} else if (!strcmp (game->name, "airtrix")) {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
	} else if (!strcmp (game->name, "braveff")) {
		eprom_bank_size = 2;
		maskrom_bank_size = 8;
	} else if (!strcmp (game->name, "pharrier")) {
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
	} else if (!strcmp (game->name, "podrace")) {
		/* XXX doesn't pass the ROMBD test */
		eprom_bank_size = 4;
		maskrom_bank_size = 8;
		maskrom_is_stretched = true;
	} else if (!strcmp (game->name, "sgnascar")) {
		//rombd_offs = 8;
		eprom_bank_size = 4;
		maskrom_bank_size = 16;
		maskrom_is_stretched = true;
	} else
		return -1;

	config->has_rom = has_rom;

	/* Set the ROMBD data layout within the external bus */
	if (has_rom) {
		/* There are four EPROM banks */
		config->eprom_bank[0] = rombd_offs + 0x10;
		config->eprom_bank[1] = rombd_offs + 0x13;
		/* There are sixteen MASKROM banks */
		/* XXX how does MASKROM stretching affect this? */
		config->maskrom_bank[0] = (rombd_offs == 0) ? 0x20 : 0x30;
		config->maskrom_bank[1] = (rombd_offs == 0) ? 0x2F : 0x4F;
		/* Set the remaining configuration btis */
		config->eprom_bank_size = eprom_bank_size;
		config->maskrom_bank_size = maskrom_bank_size;
		config->maskrom_is_stretched = maskrom_is_stretched;
	}
	/* The EEPROM bank is constant */
	config->eeprom_bank = rombd_offs + 0x14;

	return 0;
}

/* XXX actually make sh4_t opaque and add accessors for the registers; that's
 * going to be needed for any possible future debugger anyway. */
#define R(n_)	ctx->r[n_]
#define PR	ctx->pr
#define T	ctx->sr.bit.t

static uint32_t
patch_airtrix (vk_cpu_t *cpu, uint32_t pc, uint32_t inst)
{
	sh4_t *ctx = (sh4_t *) cpu;

	switch (pc) {
	case 0x0C010F9A:
		/* Make the 'WARNING' screen faster (well, 656 frames faster) */
		R(2) = 0x290;
		break;
	}
	return inst;
}

static uint32_t
patch_braveff (vk_cpu_t *cpu, uint32_t pc, uint32_t inst)
{
	sh4_t *ctx = (sh4_t *) cpu;

	switch (pc) {
	case 0x0C0D522A:
		T = 1;
		break;
	case 0x0C05B53E:
		T = 0;
		break;
	}
	return inst;
}

static uint32_t
patch_pharrier (vk_cpu_t *cpu, uint32_t pc, uint32_t inst)
{
	switch (pc) {
	case 0x0C01C322:
		/* Patches an AICA-related while (1) into a NOP */
		return 0x0009;
	}
	return inst;
}

static uint32_t
patch_sgnascar (vk_cpu_t *cpu, uint32_t pc, uint32_t inst)
{
	sh4_t *ctx = (sh4_t *) cpu;

	switch (pc) {
	case 0x0C00BC9A:
		/* Make the (non-existent) EEPROM data conform the ROM
		 * information. This is likely a region/hw version check. */
		R(3) = 0xFF;
		break;
	case 0x0C0130CE:
		/* Skip the "BAD IO BOARD" infinite loop */
		R(4) = 1;
		break;
	}
	return inst;
}

/* Install game-specific patches into the master SH-4 */
static void
hikaru_install_game_patches (hikaru_t *hikaru)
{
	vk_cpu_t *cpu = hikaru->sh_m;
	vk_game_t *game = hikaru->base.game;
	bool patched = true;

	if (!game)
		return;

	if (!strcmp (game->name, "airtrix"))
		vk_cpu_install_patch (cpu, patch_airtrix);
	else if (!strcmp (game->name, "braveff"))
		vk_cpu_install_patch (cpu, patch_braveff);
	else if (!strcmp (game->name, "pharrier"))
		vk_cpu_install_patch (cpu, patch_pharrier);
	else if (!strcmp (game->name, "sgnascar"))
		vk_cpu_install_patch (cpu, patch_sgnascar);
	else
		patched = false;

	if (patched)
		VK_LOG ("Installed patches for '%s'", game->name);
}

static void
hikaru_destroy (vk_machine_t **mach_)
{
	if (mach_) {

		hikaru_t *hikaru = (hikaru_t *) *mach_;
		if (hikaru) {

			/* dump everything we got before quitting */
			hikaru_dump ((vk_machine_t *) hikaru);

			vk_device_destroy (&hikaru->memctl_m);
			vk_device_destroy (&hikaru->memctl_s);
			vk_device_destroy (&hikaru->mscomm);
			vk_device_destroy (&hikaru->mie);
			vk_device_destroy (&hikaru->gpu);

			vk_cpu_destroy (&hikaru->sh_m);
			vk_cpu_destroy (&hikaru->sh_s);

			vk_mmap_destroy (&hikaru->mmap_m);
			vk_mmap_destroy (&hikaru->mmap_s);

			vk_buffer_destroy (&hikaru->ram_m);
			vk_buffer_destroy (&hikaru->ram_s);
			vk_buffer_destroy (&hikaru->cmdram);
			vk_buffer_destroy (&hikaru->fb);
			vk_buffer_destroy (&hikaru->texram[0]);
			vk_buffer_destroy (&hikaru->texram[1]);
			vk_buffer_destroy (&hikaru->aica_ram_m);
			vk_buffer_destroy (&hikaru->aica_ram_s);
			vk_buffer_destroy (&hikaru->bram);
			vk_buffer_destroy (&hikaru->mie_ram);
		}
		free (hikaru);
		*mach_ = NULL;
	}
}

static int
hikaru_init (hikaru_t *hikaru)
{
	vk_machine_t *mach = (vk_machine_t *) hikaru;
	vk_game_t *game = mach->game;

	unk_m.mach = mach;
	unk_s.mach = mach;

	hikaru->ram_m		= vk_buffer_le32_new (32*MB, 0);
	hikaru->ram_s		= vk_buffer_le32_new (32*MB, 0);
	hikaru->cmdram		= vk_buffer_le32_new (4*MB, 0);
	hikaru->fb		= vk_buffer_le32_new (8*MB, 0);
	hikaru->texram[0]	= vk_buffer_le32_new (4*MB, 0);
	hikaru->texram[1]	= vk_buffer_le32_new (4*MB, 0);
	hikaru->aica_ram_m	= vk_buffer_le32_new (8*MB, 0);
	hikaru->aica_ram_s	= vk_buffer_le32_new (8*MB, 0);
	hikaru->mie_ram		= vk_buffer_le32_new (32*KB, 0);
	hikaru->bram		= vk_buffer_le32_new (64*KB, 0);

	if (!hikaru->ram_m || !hikaru->ram_s ||
	    !hikaru->cmdram || !hikaru->fb ||
	    !hikaru->texram[0] || !hikaru->texram[1] ||
	    !hikaru->aica_ram_m || !hikaru->aica_ram_s ||
	    !hikaru->bram || !hikaru->mie_ram)
		goto fail;

	if (game) {
		char *version;

		hikaru->bootrom 	= vk_game_get_section_data (game, "bootrom");
		hikaru->eprom   	= vk_game_get_section_data (game, "eprom");
		hikaru->maskrom 	= vk_game_get_section_data (game, "maskrom");

		/* Patch theBOOTROM  EEPROM check. Allows games to load. */
		VK_LOG ("patching BOOTROM");

		version = (char *) vk_buffer_get_ptr (hikaru->bootrom, 0xD4);
		if (!strcmp (version, "SAMURAI BootROM Version 0.84") ||
		    !strcmp (version, "SAMURAI BootROM Version 0.92"))
			vk_buffer_put (hikaru->bootrom, 2, 0x8AE, 0xE00F); /* MOV R0, 0xFFFFFFFF */
		else if (!strcmp (version, "SAMURAI BootROM Version 0.96"))
			vk_buffer_put (hikaru->bootrom, 2, 0x8E6, 0xE00F); /* MOV R0, 0xFFFFFFFF */
		else {
			VK_ERROR ("unknown BOOTROM version!");
			goto fail;
		}

		if (hikaru_set_rombd_config (hikaru))
			goto fail;
	} else {
		/* Create a mock bootrom */                                     
		hikaru->bootrom = vk_buffer_le32_new (2*MB, 0);
	}

	hikaru->memctl_m = hikaru_memctl_new (mach, true);
	hikaru->memctl_s = hikaru_memctl_new (mach, false);

	if (!hikaru->memctl_m || !hikaru->memctl_s)
		goto fail;

	hikaru->mscomm = hikaru_mscomm_new (mach);
	if (!hikaru->mscomm)
		goto fail;

	hikaru->mie = hikaru_mie_new (mach);
	if (!hikaru->mie)
		goto fail;

	hikaru->base.renderer = hikaru_renderer_new (hikaru->fb,
	                                             hikaru->texram);

	hikaru->gpu = hikaru_gpu_new (mach, hikaru->cmdram, hikaru->fb,
	                              hikaru->texram, hikaru->base.renderer);

	if (!hikaru->gpu || !hikaru->base.renderer)
		goto fail;

	hikaru_renderer_set_gpu (hikaru->base.renderer, hikaru->gpu);

	hikaru->aica_m = hikaru_aica_new (mach, hikaru->aica_ram_m, true);
	hikaru->aica_s = hikaru_aica_new (mach, hikaru->aica_ram_m, false);

	if (!hikaru->aica_m || !hikaru->aica_s)
		goto fail;

	hikaru->mmap_m = setup_master_mmap (hikaru);
	hikaru->mmap_s = setup_slave_mmap (hikaru);

	if (!hikaru->mmap_m || !hikaru->mmap_s)
		goto fail;

	hikaru->sh_m = sh4_new (mach, hikaru->mmap_m, true, true);
	hikaru->sh_s = sh4_new (mach, hikaru->mmap_s, false, true);

	if (!hikaru->sh_m || !hikaru->sh_s)
		goto fail;

	sh4_set_porta_handlers (hikaru->sh_m, porta_get_m, porta_put_m);
	sh4_set_porta_handlers (hikaru->sh_s, porta_get_s, porta_put_s);

	hikaru_install_game_patches (hikaru);

	return 0;

fail:
	hikaru_destroy (&mach);
	return -1;
}

struct vk_machine_t *
hikaru_new (vk_game_t *game)
{
	hikaru_t *hikaru;
	vk_machine_t *mach;

	hikaru = ALLOC (hikaru_t);
	if (!hikaru)
		return NULL;

	mach = (vk_machine_t *) hikaru;
	strcpy (mach->name, "SEGA Hikaru");

	mach->game = game;

	mach->destroy		= hikaru_destroy;
	mach->reset		= hikaru_reset;
	mach->run_frame		= hikaru_run_frame;
	mach->load_state	= hikaru_load_state;
	mach->save_state	= hikaru_save_state;
	mach->get_debug_string	= hikaru_get_debug_string;

	if (hikaru_init (hikaru))
		hikaru_destroy (&mach);

	return mach;
}
