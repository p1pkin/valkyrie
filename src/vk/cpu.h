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

#ifndef __VK_CPU_H__
#define __VK_CPU_H__

#include "vk/mmap.h"
#include "vk/device.h"
#include "vk/machine.h"

typedef enum {
	VK_CPU_STATE_STOP,
	VK_CPU_STATE_SLEEP,
	VK_CPU_STATE_STANDBY,
	VK_CPU_STATE_RUN,

	VK_NUM_CPU_STATES
} vk_cpu_state_t;

typedef enum {
	VK_IRQ_STATE_CLEAR,
	VK_IRQ_STATE_RAISED,

	VK_NUM_IRQ_STATES
} vk_irq_state_t;

typedef struct vk_cpu_t vk_cpu_t;

typedef uint32_t (* vk_cpu_patch_t)(vk_cpu_t *, uint32_t, uint32_t);

struct vk_cpu_t {
	vk_device_t	base;
	vk_mmap_t	*mmap;
	vk_cpu_state_t	 state;
	int remaining;
	vk_cpu_patch_t	patch;

	int		 (* run) (vk_cpu_t *cpu, int cycles);
	void		 (* set_state) (vk_cpu_t *cpu, vk_cpu_state_t state);
	int		 (* set_irq_state) (vk_cpu_t *cpu, unsigned num, vk_irq_state_t state);
	const char	*(* get_debug_string) (vk_cpu_t *cpu);
};

#define VK_CPU_ALLOC(derivedptr_, mach_, mmap_) \
	do { \
		vk_device_t *dev; \
		vk_cpu_t *cpu; \
	\
		VK_ASSERT (mach_); \
		VK_ASSERT (mmap_); \
	\
		(derivedptr_) = ALLOC (typeof (*(derivedptr_))); \
		if (!(derivedptr_)) \
			break; \
	\
		dev = (vk_device_t *) (derivedptr_); \
		dev->mach = (mach_); \
	\
		vk_machine_register_device ((mach_), (void *) dev); \
	\
		cpu = (vk_cpu_t *) (derivedptr_); \
		cpu->mmap = (mmap_); \
	\
		vk_machine_register_cpu ((mach_), (void *) cpu); \
	\
	} while (0)

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

static inline int
vk_cpu_run (vk_cpu_t *cpu, int cycles)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (cpu->run != NULL);
	VK_ASSERT (cycles >= 0);
	return cpu->run (cpu, cycles);
}

static inline void
vk_cpu_set_state (vk_cpu_t *cpu, vk_cpu_state_t state)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (cpu->set_state);
	VK_ASSERT (state < VK_NUM_CPU_STATES);
	cpu->set_state (cpu, state);
}

static inline int
vk_cpu_set_irq_state (vk_cpu_t *cpu, unsigned num, vk_irq_state_t state)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (cpu->set_irq_state);
	VK_ASSERT (state < VK_NUM_IRQ_STATES);
	return cpu->set_irq_state (cpu, num, state);
}

static inline const char *
vk_cpu_get_debug_string (vk_cpu_t *cpu)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (cpu->get_debug_string);
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

static inline void
vk_cpu_install_patch (vk_cpu_t *cpu, vk_cpu_patch_t patch)
{
	VK_ASSERT (cpu != NULL);
	VK_ASSERT (patch != NULL);
	cpu->patch = patch;
}

static inline uint32_t
vk_cpu_patch (vk_cpu_t *cpu, uint32_t pc, uint32_t inst)
{
	VK_ASSERT (cpu);
	if (cpu->patch)
		return cpu->patch (cpu, pc, inst);
	return inst;
}

#endif /* __VK_CPU_H__ */
