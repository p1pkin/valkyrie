
#include <string.h>

#include "vk/core.h"
#include "vk/surface.h"

/* Only RGBA data is supported right now; RGBA4 and RGBA8 pixel formats are
 * supported. */

static unsigned
bpp_for_format (GLuint format)
{
	switch (format) {
	case GL_RGBA4:
		return 2;
	case GL_RGBA8:
		return 4;
	}
	return 0;
}

vk_surface_t *
vk_surface_new (unsigned width, unsigned height, GLuint format)
{
	unsigned bpp = bpp_for_format (format); /* bytes per pixel */
	if (!bpp) {
		VK_ERROR ("invalid format %X", format);
		return NULL;
	}
	if (!width || !height) {
		VK_ERROR ("invalid size (%u,%u)", width, height);
		return NULL;
	}

	vk_surface_t *surface = ALLOC (vk_surface_t);
	if (!surface)
		goto fail;

	surface->width = width;
	surface->height = height;
	surface->pitch = width * bpp;
	surface->format = format;

	surface->data = (uint8_t *) calloc (1, width * height * 4);
	if (!surface->data)
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

	/* Upload data */
	glTexImage2D (GL_TEXTURE_2D,	/* target */
	              0,		/* level */
	              format,		/* internalFormat */
	              width, height,
	              0,		/* border */
	              GL_RGBA,		/* format */
	              GL_UNSIGNED_BYTE,	/* type */
	              surface->data);	/* pixels */

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

		if (surface->id)
			glDeleteTextures (1, &surface->id);

		free (surface->data);
		free (surface);
		*surface_ = NULL;
	}
}

void
vk_surface_clear (vk_surface_t *surface)
{
	memset (surface->data, 0xFF, surface->height * surface->pitch);
}

void
vk_surface_put16 (vk_surface_t *surface, unsigned x, unsigned y, uint16_t val)
{
	if (x < surface->width && y < surface->height)
		*(uint16_t *) &surface->data[y * surface->pitch + x * 2] = val;
}

void
vk_surface_put32 (vk_surface_t *surface, unsigned x, unsigned y, uint32_t val)
{
	if (x < surface->width && y < surface->height)
		*(uint32_t *) &surface->data[y * surface->pitch + x * 4] = val;
}

void
vk_surface_commit (vk_surface_t *surface)
{
	/* Bind type to the texture */
	glBindTexture (GL_TEXTURE_2D, surface->id);

	/* Upload texture data */
	glTexSubImage2D (GL_TEXTURE_2D, /* target */
	                 0,		/* level */
	                 0,		/* xoffset */
	                 0,		/* yoffset */
	                 surface->width,
	                 surface->height,
	                 GL_RGBA,	/* format */
	                 GL_UNSIGNED_BYTE,
	                 surface->data);
}

