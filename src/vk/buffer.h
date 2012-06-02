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

#ifndef __VK_BUFFER_H__
#define __VK_BUFFER_H__

#include <string.h>

#include "vk/core.h"

typedef struct vk_buffer_t vk_buffer_t;

struct vk_buffer_t {
	uint8_t *ptr;
	unsigned size;
	uint64_t (* get) (vk_buffer_t *buf, unsigned size, uint32_t addr);
	void	 (* put) (vk_buffer_t *buf, unsigned size, uint32_t addr, uint64_t val);
};

static inline bool
is_size_valid (unsigned size)
{
	return size == 1 || size == 2 || size == 4 || size == 8;
}

static inline int
set_ptr (void *ptr, unsigned size, uint64_t val)
{
	if (!is_size_valid (size))
		return -1;
	switch (size) {
	case 1:
		*(uint8_t *) ptr = (uint8_t) val;
		break;
	case 2:
		*(uint16_t *) ptr = (uint16_t) val;
		break;
	case 4:
		*(uint32_t *) ptr = (uint32_t) val;
		break;
	default:
		*(uint64_t *) ptr = (uint64_t) val;
		break;
	}
	return 0;
}

vk_buffer_t	*vk_buffer_new (unsigned size, unsigned alignment);
vk_buffer_t	*vk_buffer_new_from_file (const char *path, unsigned size);
vk_buffer_t	*vk_buffer_new_from_file_any_size (const char *path);
vk_buffer_t	*vk_buffer_le32_new (unsigned size, unsigned alignment);
vk_buffer_t	*vk_buffer_be32_new (unsigned size, unsigned alignment);
void		 vk_buffer_delete (vk_buffer_t **buffer_);
unsigned	 vk_buffer_get_size (vk_buffer_t *buf);
const void	*vk_buffer_get_ptr (vk_buffer_t *buf, unsigned offs);
void		 vk_buffer_clear (vk_buffer_t *buffer);
void		 vk_buffer_print (vk_buffer_t *buffer);
void		 vk_buffer_print_some (vk_buffer_t *, unsigned lo, unsigned hi);
void		 vk_buffer_dump (vk_buffer_t *buffer, const char *path);

static inline uint64_t
vk_buffer_get (vk_buffer_t *buf, unsigned size, uint32_t addr)
{
	VK_ASSERT (buf);
	return buf->get (buf, size, addr);
}

static inline void
vk_buffer_put (vk_buffer_t *buf, unsigned size, uint32_t addr, uint64_t val)
{
	VK_ASSERT (buf);
	buf->put (buf, size, addr, val);
}

#endif /* _VK_BUFFER_H__ */
