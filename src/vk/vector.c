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

#include "vk/vector.h"

vk_vector_t *
vk_vector_new (unsigned base_size, unsigned element_size)
{
	vk_vector_t *vector;

	VK_ASSERT (base_size);
	VK_ASSERT (element_size);

	vector = ALLOC (vk_vector_t);
	if (!vector)
		return NULL;

	vector->data = (uint8_t *) calloc (base_size, element_size);
	if (!vector->data)
		goto fail;

	vector->size = base_size;
	vector->element_size = element_size;

	return vector;

fail:
	vk_vector_delete (&vector);
	return NULL;
}

void
vk_vector_delete (vk_vector_t **vector_)
{
	if (vector_) {
		vk_vector_t *vector = (vk_vector_t *) *vector_;
		if (vector)
			free (vector->data);
		free (vector);
		*vector_ = NULL;
	}
}

static void
resize_if_required (vk_vector_t *vector)
{
	VK_ASSERT (vector);

	if (vector->used == vector->size) {

		VK_ASSERT (vector->size);

		vector->size *= 2;
		vector->data = (uint8_t *)
			realloc ((void *) vector->data,
			         vector->size * vector->element_size);
		VK_ASSERT (vector->data);
	}
}

void *
vk_vector_append_entry (vk_vector_t *vector)
{
	void *entry;
	resize_if_required (vector);
	entry = (void *) &vector->data[vector->used];
	vector->used += vector->element_size;
	memset (entry, 0, vector->element_size);
	return entry;
}

void
vk_vector_append (vk_vector_t *vector, void *key)
{
	void **entry = (void **) vk_vector_append_entry (vector);
	*entry = key;
}

void
vk_vector_clear (vk_vector_t *vector)
{
	VK_ASSERT (vector);

	memset (vector->data, 0, vector->element_size * vector->size);
	vector->used = 0;
}
