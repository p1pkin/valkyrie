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

#include "vk/state.h"

#define VK_STATE_VERSION	1

vk_state_t *
vk_state_new (const char *path, uint32_t mode)
{
	char template[32], header[32];
	vk_state_t *state;
	size_t size;

	VK_ASSERT (path);
	VK_ASSERT (mode == VK_STATE_LOAD || mode == VK_STATE_SAVE);

	state = ALLOC (vk_state_t);
	if (!state)
		return NULL;

	state->fp = fopen (path, (mode == VK_STATE_LOAD) ? "rb" : "wb");
	if (!state->fp)
		goto fail;

	state->mode = mode;

	sprintf (template, "valkyrie state %08X\n", VK_STATE_VERSION);
	if (mode == VK_STATE_LOAD) {
		size = fread ((void *) header, 1, sizeof (template), state->fp);
		if (size != sizeof (template) || strcmp (template, header))
			goto fail;
	} else {
		size = fwrite ((void *) template, 1, sizeof (template), state->fp);
		if (size != sizeof (template))
			goto fail;
	}

	return state;

fail:
	vk_state_destroy (&state, -1);
	return NULL;
}

void
vk_state_destroy (vk_state_t **state_, int ret)
{
	vk_state_t *state;

	VK_ASSERT (state_);
	state = *state_;

	if (state->mode == VK_STATE_LOAD ||
	    (state->mode == VK_STATE_SAVE && ret == 0))
		fclose (state->fp);

	free (state);
	*state_ = NULL;
}

int
vk_state_put (vk_state_t *state, void *src, uint32_t size)
{
	size_t num;

	if (state->mode != VK_STATE_SAVE)
		return -1;

	num = fwrite (src, 1, size, state->fp);
	return (num != size || ferror (state->fp)) ? -1 : 0;
}

int
vk_state_get (vk_state_t *state, void *dst, uint32_t size)
{
	size_t num;

	if (state->mode != VK_STATE_LOAD)
		return -1;

	num = fread (dst, 1, size, state->fp);
	return (num != size || ferror (state->fp)) ? -1 : 0;
}
