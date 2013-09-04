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

#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#include "vk/machine.h"

typedef struct vk_device_t vk_device_t;

struct vk_device_t {
	vk_machine_t *mach;
	unsigned flags;

	void	(* destroy)(vk_device_t **dev_);
	void	(* reset)(vk_device_t *dev, vk_reset_type_t type);
	int	(* exec)(vk_device_t *dev, int cycles);
	int	(* get)(vk_device_t *dev, unsigned size, uint32_t addr, void *val);
	int	(* put)(vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val);
	int	(* save_state)(vk_device_t *dev, vk_state_t *state);
	int	(* load_state)(vk_device_t *dev, vk_state_t *state);
};

#define VK_DEVICE_ALLOC(derivedptr_, mach_) \
	do { \
		vk_device_t *base; \
	\
		VK_ASSERT (mach_); \
	\
		(derivedptr_) = ALLOC (typeof (*(derivedptr_))); \
		if (!(derivedptr_)) \
			break; \
	\
		base = &((derivedptr_)->base); \
		base->mach = (mach_); \
	\
		vk_machine_register_device ((mach_), (void *) base); \
	\
	} while (0)

static inline void
vk_device_destroy (vk_device_t **dev_)
{
	if (dev_) {
		(*dev_)->destroy (dev_);
	}
}

static inline void
vk_device_reset (vk_device_t *dev, vk_reset_type_t type)
{
	VK_ASSERT (dev);
	VK_ASSERT (dev->reset);
	VK_ASSERT (type < VK_NUM_RESET_TYPES);
	dev->reset (dev, type);
}

static inline int
vk_device_exec (vk_device_t *dev, int cycles)
{
	VK_ASSERT (dev);
	VK_ASSERT (cycles);
	if (dev->exec)
		return dev->exec (dev, cycles);
	/* As if all cycles were consumed by the device */
	return cycles;
}

static inline int
vk_device_get (vk_device_t *dev, unsigned size, uint32_t addr, void *val)
{
	VK_ASSERT (dev);
	VK_ASSERT (dev->get);
	VK_ASSERT (val);
	return dev->get (dev, size, addr, val);
}

static inline int
vk_device_put (vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val)
{
	VK_ASSERT (dev);
	VK_ASSERT (dev->put);
	return dev->put (dev, size, addr, val);
}

static inline int
vk_device_save_state (vk_device_t *dev, vk_state_t *state)
{
	VK_ASSERT (dev);
	VK_ASSERT (state);
	if (dev->save_state)
		return dev->save_state (dev, state);
	return 0;
}

static inline int
vk_device_load_state (vk_device_t *dev, vk_state_t *state)
{
	VK_ASSERT (dev);
	VK_ASSERT (state);
	if (dev->load_state)
		return dev->load_state (dev, state);
	return 0;
}

static inline void
vk_device_log (vk_device_t *device, const char *fmt, ...)
{
}

static inline void
vk_device_error (vk_device_t *device, const char *fmt, ...)
{
}

static inline void
vk_device_abort (vk_device_t *device)
{
}

static inline void
vk_device_assert (vk_device_t *device, bool cond)
{
}

#endif /* __VK_DEVICE_H__ */
