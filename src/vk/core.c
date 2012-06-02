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

#include <string.h>
#include <errno.h>
#include <math.h>

#include "vk/types.h"
#include "vk/core.h"

bool
is_valid_mat4x3f (mtx4x3f_t m)
{
	unsigned i, j;
	float norm = 0.0f;
	/* Compute the Froboenius norm */
	for (i = 0; i < 4; i++)
		for (j = 0; j < 3; j++)
			norm += m[i][j] * m[i][j];
	norm = sqrtf (norm);
	return (norm > 0.0) && isfinite (norm);
}

bool
is_valid_mat4x4f (mtx4x4f_t m)
{
	unsigned i, j;
	float norm = 0.0f;
	/* Compute the Froboenius norm */
	for (i = 0; i < 4; i++)
		for (j = 0; j < 3; j++)
			norm += m[i][j] * m[i][j];
	norm = sqrtf (norm);
	return (norm > 0.0) && isfinite (norm);
}


bool
vk_util_get_bool_option (const char *name, bool fallback)
{
	char *env = getenv (name);
	if (env) {
		if (!strcasecmp (env, "TRUE") || !strcmp (env, "1"))
			return true;
		if (!strcasecmp (env, "FALSE") || !strcmp (env, "0"))
			return false;
	}
	return fallback;
}

int
vk_util_get_int_option (const char *name, int fallback)
{
	char *env = getenv (name);
	if (env)
		return atoi (env);
	return fallback;
}

int
vk_hexstrtoi (char *str, int *res)
{
	uint32_t offs = 0;
	int len, i;
	*res = 0;
	if (str) {
		len = (int) strlen (str);
		if (len >= 2 && str[0] == '0' && str[1] == 'x')
			str = &str[2];
		for (i = len - 1; i > 0; i--) {
			if (str[i] >= '0' && str[i] <= '9')
				offs += (int) (str[i] - '0');
			else if (str[i] >= 'a' && str[i] <= 'f')
				offs += (int) (str[i] - 'a' + 10);
			else if (str[i] >= 'A' && str[i] <= 'F')
				offs += (int) (str[i] - 'A' + 10);
			else
				return 1;
			offs *= 16;
		}
	}
	return 0;
}

void
vk_strarray_fprint (FILE *fp, char **strarray, char sep)
{
	if (strarray) {
		unsigned i;
		for (i = 0; strarray[i] != NULL; i++)
			fprintf (fp, "%s%c", strarray[i], sep);
	}
}

#if 0

void
vk_strarray_print (char **strarray, char sep)
{
	vk_strarray_fprint (stdout, strarray, sep);
}

void
vk_strarray_free (char **strarray)
{
	if (strarray) {
		unsigned i;
		for (i = 0; strarray[i] != NULL; i++)
			free (strarray[i]);
		free (strarray);
	}
}

char **
vk_strsplit (char *str, char sep)
{
	static char **out;
	unsigned num, i;
	bool done;

	if (!str || sep == '\0')
		return NULL;

	/* Count how many separators in string */
	for (i = 0, num = 0; str[i] != '\0'; i++)
		if (str[i] == sep)
			num ++;

	/* Allocate string array plus one extra NULL entry */
	out = (char **) calloc (num + 3, sizeof (char *));
	if (!out)
		goto fail;

	/* Do the actual splitting */
	i = 0;
	done = false;
	do {
		char *match = strchr (str, sep);
		size_t len = match - str;
		if (match) {
			if (len > 0)
				out[i++] = strndup (str, len);
			str = &match[1];
		} else {
			out[i] = strdup (str);
			done = true;
		}
	} while (!done);

	vk_fprint_strings (stdout, out);

	return out;

fail:
	vk_free_strings (out);
	return NULL;
}

#endif

static size_t
get_file_size (FILE *fp)
{
	size_t size, offs;
	offs = (size_t) ftell (fp);
	fseek (fp, 0, SEEK_END);
	size = ftell (fp);
	fseek (fp, offs, SEEK_SET);
	return size;
}

int
vk_load (uint8_t *buf, const char *path, size_t req)
{
	FILE *fp;
	size_t size;
	int ret = ENOENT;

	do {
		fp = fopen (path, "rb");
		if (!fp) {
			fprintf (stderr, "ERROR: file '%s' not found\n", path);
			break;
		}
		size = get_file_size (fp);
		if (size != req) {
			fprintf (stderr, "ERROR: size mismatch for '%s' (gottend %u bytes out of %u)\n", path, size, req);
			break;
		}
		clearerr (fp);
		size = fread (buf, 1, req, fp);
		if (ferror (fp)) {
			perror (NULL);
			break;
		}
		if (size != req) {
			fprintf (stderr, "ERROR: can't read '%s' (gotten %u bytes out of %u)\n", path, size, req);
			break;
		}
		ret = 0;
	} while (0);

	if (fp) {
		fclose (fp);
	}

	return ret;
}

void *
vk_load_any (const char *path, size_t *_size)
{
	FILE *fp = NULL;
	size_t size, size2;
	void *buf = NULL;
	int ret = -1;

	do {
		if (!path) {
			break;
		}
		if (_size) {
			*_size = 0;
		}
		fp = fopen (path, "r");
		if (!fp) {
			break;
		}
		size = get_file_size (fp);
		if (!size) {
			break;
		}
		buf = calloc (1, size+1);
		if (!buf) {
			break;
		}
		size2 = fread (buf, 1, size, fp);
		if (size2 != size) {
			break;
		}

		((char *) buf) [size] = '\0';

		if (_size) {
			*_size = size;
		}

		ret = 0;

	} while (0);

	if (fp) {
		fclose (fp);
	}

	if (ret && buf) {
		free (buf);
		buf = NULL;
	}

	return buf;
}

void
vk_swap_buf (uint8_t *buf, size_t size, vk_swap swap)
{
	size_t i;

	VK_ASSERT (buf);

	switch (swap) {
	case VK_SWAP_NONE:
		break;
	case VK_SWAP_BSWAP16:
		for (i = 0; i < size; i += 2) {
			*(uint16_t *) &buf[i] = bswap16 (*(uint16_t *) &buf[i]);
		}
		break;
	case VK_SWAP_BSWAP32:
		for (i = 0; i < size; i+= 4) {
			*(uint32_t *) &buf[i] = bswap32 (*(uint32_t *) &buf[i]);
		}
		break;
	default:
		VK_ABORT ("invalid swap: %d\n", swap);
	}
}

void
vk_interleave_buf_2 (uint8_t *dst,
                     uint8_t *a,
                     uint8_t *b,
                     size_t size,
                     size_t part)
{
	size_t i, j;

	VK_ASSERT ((size % part) == 0);

	for (i = 0, j = 0; i < size; j += part, i += part * 2) {
		memcpy (&dst[i],      &a[j], part);
		memcpy (&dst[i+part], &b[j], part);
	}
}

void
vk_interleave_buf_4 (uint8_t *dst,
                     uint8_t *a,
                     uint8_t *b,
                     uint8_t *c,
                     uint8_t *d,
                     size_t size,
                     size_t part)
{
	size_t i, j;

	VK_ASSERT ((size % part) == 0);

	for (i = 0, j = 0; i < size; j += part, i += part * 4) {
		memcpy (&dst[i],        &a[j], part);
		memcpy (&dst[i+part],   &b[j], part);
		memcpy (&dst[i+part*2], &c[j], part);
		memcpy (&dst[i+part*3], &d[j], part);
	}
}

int
vk_memcpy_interleave (uint8_t *dst, uint8_t *src, unsigned unit, unsigned offs, size_t size)
{
	size_t i;

	switch (unit) {
	case 2:
		for (i = 0; i < size; i += 2) {
			*(uint16_t *) &dst[i*2+offs] = *(uint16_t *) &src[i];
		}
		break;
	default:
		VK_ABORT ("invalid unit: %u\n", unit);
	}

	return 0;
}

