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

#ifndef __VK_VECTOR_H__
#define __VK_VECTOR_H__

#include "vk/core.h"

typedef struct {
	unsigned element_size;
	unsigned size;
	unsigned used;
	uint8_t *data;
} vk_vector_t;

vk_vector_t	*vk_vector_new (unsigned base_size, unsigned element_size);
void		 vk_vector_destroy (vk_vector_t **vector_);
void		*vk_vector_append_entry (vk_vector_t *vector);
void		 vk_vector_append (vk_vector_t *vector, void *key);
void		 vk_vector_clear (vk_vector_t *vector);

#endif /* __VK_VECTOR_H__ */
