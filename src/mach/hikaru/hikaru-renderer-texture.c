/* 
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
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

#include "mach/hikaru/hikaru-renderer-private.h"

static bool
is_texhead_eq (hikaru_renderer_t *hr,
               hikaru_texhead_t *a, hikaru_texhead_t *b)
{
	return (a->format == b->format) &&
	       (a->logw == b->logw) &&
	       (a->logh == b->logh) &&
	       (a->bank == b->bank) &&
	       (a->slotx == b->slotx) &&
	       (a->sloty == b->sloty);
}

static void
destroy_texture (hikaru_texture_t *tex)
{
	if (tex->id) {
		glDeleteTextures (1, &tex->id);
		VK_ASSERT_NO_GL_ERROR ();
	}
	memset ((void *) tex, 0, sizeof (hikaru_texture_t));
}

static GLuint
upload_texture (hikaru_renderer_t *hr, hikaru_texhead_t *th)
{
	vk_buffer_t *texram = hr->gpu->texram[th->bank];
	uint8_t *data = texram->ptr;
	uint32_t w, h, basex, basey;
	GLuint id;

	w = 16 << th->logw;
	h = 16 << th->logh;

	get_texhead_coords (&basex, &basey, th);

	VK_LOG ("TEXTURE texhead=%s base=(%u,%u)", get_texhead_str (th), basex, basey);

	glGenTextures (1, &id);
	VK_ASSERT_NO_GL_ERROR ();

	glActiveTexture (GL_TEXTURE0 + 0);
	VK_ASSERT_NO_GL_ERROR ();

	glBindTexture (GL_TEXTURE_2D, id);
	VK_ASSERT_NO_GL_ERROR ();

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
	                 (th->wrapu == 0) ? GL_CLAMP_TO_EDGE :
	                 (th->repeatu == 0) ? GL_REPEAT : GL_MIRRORED_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
	                 (th->wrapv == 0) ? GL_CLAMP_TO_EDGE :
	                 (th->repeatv == 0) ? GL_REPEAT : GL_MIRRORED_REPEAT);
	VK_ASSERT_NO_GL_ERROR ();

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	VK_ASSERT_NO_GL_ERROR ();

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	VK_ASSERT_NO_GL_ERROR ();

	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	switch (th->format) {
	case HIKARU_FORMAT_ABGR1555:
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 2048);
		glPixelStorei (GL_UNPACK_SKIP_ROWS, basey);
		glPixelStorei (GL_UNPACK_SKIP_PIXELS, basex);
		VK_ASSERT_NO_GL_ERROR ();

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGB5_A1,
		              w, h, 0,
		              GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
		              data);
		VK_ASSERT_NO_GL_ERROR ();
		break;
	case HIKARU_FORMAT_ABGR4444:
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 2048);
		glPixelStorei (GL_UNPACK_SKIP_ROWS, basey);
		glPixelStorei (GL_UNPACK_SKIP_PIXELS, basex);
		VK_ASSERT_NO_GL_ERROR ();

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGBA4,
		              w, h, 0,
		              GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV,
		              data);
		VK_ASSERT_NO_GL_ERROR ();
		break;
	default:
		goto fail;
	}

	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei (GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei (GL_UNPACK_SKIP_PIXELS, 0);
	VK_ASSERT_NO_GL_ERROR ();

	return id;

fail:
	return 0;
}

hikaru_texture_t *
hikaru_renderer_get_texture (hikaru_renderer_t *hr, hikaru_texhead_t *th)
{
	hikaru_texture_t *cached;
	uint32_t bank, slotx, sloty;
	GLuint id;

	bank  = th->bank;
	slotx = th->slotx;
	sloty = th->sloty;

	if (slotx < 0x80 || sloty < 0xC0)
		return NULL;

	slotx -= 0x80;
	sloty -= 0xC0;

	cached = &hr->textures.cache[bank][sloty][slotx];
	if (is_texhead_eq (hr, th, &cached->th))
		return cached;

	destroy_texture (cached);

	id = upload_texture (hr, th);
	if (!id) {
		destroy_texture (cached);
		return NULL;
	}

	cached->th = *th;
	cached->id = id;

	hr->textures.is_clear[bank] = false;
	return cached;
}

static void
clear_texcache_bank (hikaru_renderer_t *hr, unsigned bank)
{
	unsigned x, y;

	if (hr->textures.is_clear[bank])
		return;
	hr->textures.is_clear[bank] = true;

	/* Free all allocated surfaces. */
	for (y = 0; y < 0x40; y++)
		for (x = 0; x < 0x80; x++)
			destroy_texture (&hr->textures.cache[bank][y][x]);

	/* Zero out the cache itself, to avoid spurious hits. Note that
	 * texture RAM origin is (80,C0), so (slotx, sloty) will never match
	 * a zeroed out cache entries. */
	memset ((void *) &hr->textures.cache[bank], 0, sizeof (hr->textures.cache[bank]));
}

void
hikaru_renderer_invalidate_texcache (vk_renderer_t *rend, hikaru_texhead_t *th)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;

	VK_ASSERT (hr);

	if (th == NULL) {
		clear_texcache_bank (hr, 0);
		clear_texcache_bank (hr, 1);
	} else
		clear_texcache_bank (hr, th->bank);
}
