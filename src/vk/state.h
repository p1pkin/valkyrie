/* 
 * Valkyrie
 * Copyright (C) 2011-2013, Stefano Teso
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

#ifndef __VK_STATE_H__
#define __VK_STATE_H__

#include "vk/core.h"

#define VK_STATE_SAVE	(0 << 0)
#define VK_STATE_LOAD	(1 << 0)

typedef struct {
	uint32_t mode;
	FILE *fp;
} vk_state_t;

vk_state_t	*vk_state_new (const char *path, uint32_t mode);
int		 vk_state_put (vk_state_t *state, void *src, uint32_t size);
int		 vk_state_get (vk_state_t *state, void *dst, uint32_t size);
void		 vk_state_destroy (vk_state_t **state_, int ret);

#endif /* __VK_STATE_H__ */
