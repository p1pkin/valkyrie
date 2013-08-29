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

#include "vk/core.h"
#include "vk/buffer.h"

int
main (int argc, char **argv)
{
	vk_buffer_t *buf = NULL;
	uint32_t *val32, i;
	uint16_t *val16;
	int ret = 1;

	if (argc < 4) {
		printf ("Usage: %s mode input output\n", argv[0]);
		goto fail;
	}

	buf = vk_buffer_new_from_file (argv[2], ~0);
	if (!buf) {
		fprintf (stderr, "ERROR: file '%s' not found\n", argv[2]);
		goto fail;
	}

	switch (atoi (argv[1])) {
	case 816:
		val16 = (uint16_t *) vk_buffer_get_ptr (buf, 0);
		for (i = 0; i < vk_buffer_get_size (buf); i += 2, val16++)
			*val16 = bswap16 (*val16);
		break;
	case 832:
		val32 = (uint32_t *) vk_buffer_get_ptr (buf, 0);
		for (i = 0; i < vk_buffer_get_size (buf); i += 4, val32++)
			*val32 = bswap32 (*val32);
		break;
	default:
		fprintf (stderr, "ERROR: invalid mode '%s'\n", argv[1]);
		goto fail;
	}

	vk_buffer_dump (buf, argv[3]);
	ret = 0;

fail:
	vk_buffer_destroy (&buf);
	return ret;
}
