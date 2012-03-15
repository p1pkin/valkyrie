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

#ifndef __VK_CPU_H__
#define __VK_CPU_H__

#include "vk/mmap.h"
#include "vk/machine.h"

typedef enum {
	VK_CPU_STATE_STOP,
	VK_CPU_STATE_SLEEP,
	VK_CPU_STATE_STANDBY,
	VK_CPU_STATE_RUN,
	VK_NUM_CPU_STATES,
} vk_cpu_state_t;

typedef enum {
	VK_IRQ_STATE_CLEAR,
	VK_IRQ_STATE_RAISED,
	VK_NUM_IRQ_STATES,
} vk_irq_state_t;

typedef struct vk_cpu_t vk_cpu_t;

struct vk_cpu_t {
	vk_machine_t	*mach;
	vk_mmap_t	*mmap;
	vk_cpu_state_t	 state;
	int remaining;

	void		 (* delete) (vk_cpu_t **cpu_);
	void		 (* reset) (vk_cpu_t *cpu, vk_reset_type_t type);
	int		 (* run) (vk_cpu_t *cpu, int cycles);
	void		 (* set_state) (vk_cpu_t *cpu, vk_cpu_state_t state);
	int		 (* set_irq_state) (vk_cpu_t *cpu, unsigned num, vk_irq_state_t state);
	const char	*(* get_debug_string) (vk_cpu_t *cpu);
};

#define VK_CPU_LOG(cpu_, fmt_, args_...) \
	do { \
		const char *str = vk_cpu_get_debug_string ((vk_cpu_t *) cpu_); \
		VK_LOG ("%s : "fmt_, str, ##args_); \
	} while (0);

#define VK_CPU_ERROR(cpu_, fmt_, args_...) \
	do { \
		const char *str = vk_cpu_get_debug_string ((vk_cpu_t *) cpu_); \
		VK_ERROR ("%s : "fmt_, str, ##args_); \
	} while (0);

#define VK_CPU_ABORT(cpu_, fmt_, args_...) \
	do { \
		const char *str = vk_cpu_get_debug_string ((vk_cpu_t *) cpu_); \
		VK_ABORT ("%s : "fmt_, str, ##args_); \
	} while (0);

#define VK_CPU_ASSERT(cpu_, cond_) \
	do { \
		if (!(cond_)) { \
			const char *str = vk_cpu_get_debug_string ((vk_cpu_t *) cpu_); \
			VK_ABORT ("%s : assertion failed, aborting", str); \
		} \
	} while (0);

static inline void
vk_cpu_delete (vk_cpu_t **cpu_)
{
	if (cpu_) {
		vk_cpu_t *cpu = *cpu_;
		if (cpu->delete)
			(*cpu_)->delete (cpu_);
	}
}

static inline void
vk_cpu_reset (vk_cpu_t *cpu, vk_reset_type_t type)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (cpu->run != NULL);
	cpu->reset (cpu, type);
}

static inline int
vk_cpu_run (vk_cpu_t *cpu, int cycles)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (cpu->run != NULL);
	return cpu->run (cpu, cycles);
}

static inline void
vk_cpu_set_state (vk_cpu_t *cpu, vk_cpu_state_t state)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (state < VK_NUM_CPU_STATES);
	cpu->set_state (cpu, state);
}

static inline int
vk_cpu_set_irq_state (vk_cpu_t *cpu, unsigned num, vk_irq_state_t state)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (state < VK_NUM_IRQ_STATES);
	return cpu->set_irq_state (cpu, num, state);
}

static inline const char *
vk_cpu_get_debug_string (vk_cpu_t *cpu)
{
	VK_ASSERT (cpu != NULL);
	return cpu->get_debug_string (cpu);
}

static inline int
vk_cpu_get (vk_cpu_t *cpu, unsigned size, uint32_t addr, void *val)
{
	VK_ASSERT (cpu != NULL);
	return vk_mmap_get (cpu->mmap, size, addr, val);
}

static inline int
vk_cpu_put (vk_cpu_t *cpu, unsigned size, uint32_t addr, uint64_t val)
{
	VK_ASSERT (cpu != NULL);
	return vk_mmap_put (cpu->mmap, size, addr, val);
}

#endif /* __VK_CPU_H__ */
