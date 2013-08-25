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

#ifndef __VK_HIKARU_H__
#define __VK_HIKARU_H__

#include "vk/mmap.h"
#include "vk/cpu.h"
#include "vk/machine.h"

typedef struct {
	bool has_rom;
	unsigned eprom_bank[2];
	unsigned maskrom_bank[2];
	unsigned eeprom_bank;
	uint32_t eprom_bank_size;
	uint32_t maskrom_bank_size;
	bool maskrom_is_stretched;
} hikaru_rombd_config_t;

typedef struct {
	vk_machine_t base;

	/* CPU (master) */
	vk_cpu_t *sh_m;
	vk_mmap_t *mmap_m;

	/* CPU (slave) */
	vk_cpu_t *sh_s;
	vk_mmap_t *mmap_s;

	vk_cpu_t *sh_current;

	/* Devices */
	vk_device_t *memctl_m;
	vk_device_t *memctl_s;
	vk_device_t *mscomm;
	vk_device_t *mie;
	vk_device_t *aica_m;
	vk_device_t *aica_s;
	vk_device_t *gpu;

	/* Port A (master and slave) */
	uint16_t porta_m;
	unsigned porta_m_bit0_buffer;

	uint16_t porta_s;
	unsigned porta_s_bit1_buffer;

	/* Unknown Hardware (master) */
	uint32_t unk01000000_m;
	uint32_t unk01000100_m;

	/* Unknown Hardware (Slave) */
	uint32_t unk1A800008_s;
	uint32_t unk1B000100_s;

	/* RAM areas */
	vk_buffer_t *ram_m, *ram_s;
	vk_buffer_t *cmdram;
	vk_buffer_t *texram[2];
	vk_buffer_t *fb;
	vk_buffer_t *aica_ram_m;
	vk_buffer_t *aica_ram_s;
	vk_buffer_t *mie_ram;
	vk_buffer_t *bram;

	/* ROM data */
	vk_buffer_t *bootrom;
	vk_buffer_t *eprom;
	vk_buffer_t *maskrom;

	/* ROMBD configuration */
	hikaru_rombd_config_t rombd_config;

} hikaru_t;

vk_machine_t	*hikaru_new (vk_game_t *game);
void		 hikaru_raise_gpu_irq (vk_machine_t *mach);
void		 hikaru_raise_aica_irq (vk_machine_t *mach);
void		 hikaru_raise_memctl_irq (vk_machine_t *mach);

#endif /* __VK_HIKARU_H__ */
