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

#ifndef __VK_SURFACE_H__
#define __VK_SURFACE_H__

#include "vk/core.h"

typedef enum {
	VK_SURFACE_FORMAT_RGBA4444,
	VK_SURFACE_FORMAT_RGBA5551,
	VK_SURFACE_FORMAT_RGBA8888,
	VK_NUM_SURFACE_FORMATS
} vk_surface_format_t;

typedef struct {
	unsigned id;
	vk_surface_format_t format;
	unsigned width;
	unsigned height;
	unsigned pitch;
	uint8_t *data;
} vk_surface_t;

vk_surface_t		*vk_surface_new (unsigned widht, unsigned height,
			                 vk_surface_format_t format,
			                 int wrap_u, int wrap_v); 
void			 vk_surface_destroy (vk_surface_t **surface_);
void			 vk_surface_dump (vk_surface_t *, char *path);
void			 vk_surface_clear (vk_surface_t *surface);
void			 vk_surface_commit (vk_surface_t *surface);
void			 vk_surface_bind (vk_surface_t *surface);
void			 vk_surface_draw (vk_surface_t *surface);

static inline void
vk_surface_put16 (vk_surface_t *surface, unsigned x, unsigned y, uint16_t val)
{
	uint32_t addr = y * surface->pitch + x * 2;
	VK_ASSERT (addr < (surface->height * surface->pitch));
	*(uint16_t *) &surface->data[addr] = val;
}

static inline void
vk_surface_put32 (vk_surface_t *surface, unsigned x, unsigned y, uint32_t val)
{
	uint32_t addr = y * surface->pitch + x * 4;
	VK_ASSERT (addr < (surface->height * surface->pitch));
	*(uint32_t *) &surface->data[addr] = val;
}

#endif /* __VK_SURFACE_H__ */
