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

#include <string.h>

/* TODO support mipmaps */

#include "vk/core.h"
#include "vk/surface.h"

static const struct {
	GLint iformat;	/* Specifies component resolution */
	GLenum format;	/* Specifies component order */
	GLenum type;	/* Specifies component layout in memory */
	unsigned bpp;	/* Bits-per-pixel */
} format_desc[VK_NUM_SURFACE_FORMATS] = {
	[VK_SURFACE_FORMAT_RGBA4444]	= { GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2 },
	[VK_SURFACE_FORMAT_RGBA5551]	= { GL_RGBA8, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2 },
	[VK_SURFACE_FORMAT_RGBA8888]	= { GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, 4 }
};

vk_surface_t *
vk_surface_new (unsigned width, unsigned height, vk_surface_format_t format)
{
	unsigned bpp;
	int ret;

	if (!width || !height) {
		VK_ERROR ("invalid surface size (%u,%u)", width, height);
		return NULL;
	}
	if (format >= VK_NUM_SURFACE_FORMATS) {
		VK_ERROR ("invalid surface format %u", format);
		return NULL;
	}

	vk_surface_t *surface = ALLOC (vk_surface_t);
	if (!surface)
		goto fail;

	bpp = format_desc[format].bpp;

	surface->width = width;
	surface->height = height;
	surface->pitch = width * bpp;
	surface->format = format;

	ret = posix_memalign ((void *) &surface->data, 16, width * height * bpp);
	if (ret != 0 || !surface->data)
		goto fail;

	/* Disable surface alignment */
	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);

	/* Generate a new texture name */
	glGenTextures (1, &surface->id);
	if (!surface->id)
		goto fail;

	/* Bind a type to the name */
	glBindTexture (GL_TEXTURE_2D, surface->id);

	/* Set texture parameters */
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	/* Upload the actual data */
	glTexImage2D (GL_TEXTURE_2D, 0, format_desc[format].iformat,
	              width, height, 0, format_desc[format].format,
	              format_desc[format].type, surface->data);

	return surface;
fail:
	vk_surface_delete (&surface);
	return NULL;
}

void
vk_surface_delete (vk_surface_t **surface_)
{
	if (surface_) {
		vk_surface_t *surface = *surface_;
		if (surface) {
			if (surface->id)
				glDeleteTextures (1, &surface->id);
			free (surface->data);
		}
		free (surface);
		*surface_ = NULL;
	}
}

void
vk_surface_clear (vk_surface_t *surface)
{
	if (surface)
		memset (surface->data, 0xFF, surface->height * surface->pitch);
}

void
vk_surface_commit (vk_surface_t *surface)
{
	if (!surface)
		return;

	/* Bind type to the texture */
	glBindTexture (GL_TEXTURE_2D, surface->id);

	/* Upload texture data */
	glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
	                 surface->width, surface->height,
	                 format_desc[surface->format].format,
	                 format_desc[surface->format].type,
	                 surface->data);
}

void
vk_surface_bind (vk_surface_t *surface)
{
	if (surface)
		glBindTexture (GL_TEXTURE_2D, surface->id);
}

void
vk_surface_draw (vk_surface_t *surface)
{
	/* XXX very fragile; for debug only */
	if (!surface)
		return;

	glMatrixMode (GL_PROJECTION);
	glPushMatrix ();
	glLoadIdentity ();
	glOrtho (0.0f, 640.0f,	/* left, right */
	         480.0f, 0.0f,	/* bottom, top */
	         -1.0f, 1.0f);	/* near, far */

	glMatrixMode (GL_MODELVIEW);
	glPushMatrix ();
	glLoadIdentity ();

	glEnable (GL_TEXTURE_2D);

	vk_surface_bind (surface);

	glBegin (GL_TRIANGLE_STRIP);
		glTexCoord2f (0.0f, 0.0f);
		glVertex3f (0.0f, 0.0f, 0.0f);
		glTexCoord2f (1.0f, 0.0f);
		glVertex3f (639.0f, 0.0f, 0.0f);
		glTexCoord2f (0.0f, 1.0f);
		glVertex3f (0.0f, 479.0f, 0.0f);
		glTexCoord2f (1.0f, 1.0f);
		glVertex3f (639.0f, 479.0f, 0.0f);
	glEnd ();

	glMatrixMode (GL_MODELVIEW);
	glPopMatrix ();

	glMatrixMode (GL_PROJECTION);
	glPopMatrix ();
}
