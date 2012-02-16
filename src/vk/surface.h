
#ifndef __VK_SURFACE_H__
#define __VK_SURFACE_H__

/* TODO support mipmaps */

#include <stdint.h>
#include <GL/glew.h>

typedef struct {
	GLuint id;	/**< GL id of the corresponding texture */
	GLint format;	/**< The GL internalFormat for this texture */
	unsigned width;
	unsigned height;
	unsigned pitch;
	uint8_t *data;	/**< The actual storage backing up the surface */
} vk_surface_t;

vk_surface_t		*vk_surface_new (unsigned widht, unsigned height, GLuint format);
void			 vk_surface_delete (vk_surface_t **surface_);
void			 vk_surface_clear (vk_surface_t *surface);
void			 vk_surface_put16 (vk_surface_t *surface, unsigned x, unsigned y, uint16_t val);
void			 vk_surface_put32 (vk_surface_t *surface, unsigned x, unsigned y, uint32_t val);
void			 vk_surface_commit (vk_surface_t *surface);

#endif /* __VK_SURFACE_H__ */
