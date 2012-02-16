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

#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#include "vk/machine.h"

typedef struct vk_device_t vk_device_t;

struct vk_device_t {
	vk_machine_t *mach;
	unsigned flags;

	void	(* delete)(vk_device_t **dev_);
	void	(* reset)(vk_device_t *dev, vk_reset_type_t type);
	int	(* exec)(vk_device_t *dev, int cycles);
	int	(* get)(vk_device_t *dev, unsigned size, uint32_t addr, void *val);
	int	(* put)(vk_device_t *dev, unsigned size, uint32_t addr, uint64_t val);
	int	(* save_state)(vk_device_t *dev, FILE *fp);
	int	(* load_state)(vk_device_t *dev, FILE *fp);
};

static inline void
vk_device_delete (vk_device_t **dev_)
{
	if (dev_)
		(*dev_)->delete (dev_);
}

static inline void
vk_device_reset (vk_device_t *dev, vk_reset_type_t type)
{
	VK_ASSERT (dev);
	VK_ASSERT (dev->reset);
	dev->reset (dev, type);
}

static inline int
vk_device_exec (vk_device_t *dev, int cycles)
{
	VK_ASSERT (dev);
	VK_ASSERT (dev->exec);
	VK_ASSERT (cycles);
	return dev->exec (dev, cycles);
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
vk_device_save_state (vk_device_t *dev, FILE *fp)
{
	VK_ASSERT (dev);
	VK_ASSERT (dev->save_state);
	VK_ASSERT (fp);
	return dev->save_state (dev, fp);
}

static inline int
vk_device_load_state (vk_device_t *dev, FILE *fp)
{
	VK_ASSERT (dev);
	VK_ASSERT (dev->load_state);
	VK_ASSERT (fp);
	return dev->load_state (dev, fp);
}

#endif /* __VK_DEVICE_H__ */
