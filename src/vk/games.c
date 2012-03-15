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

#include <vk/core.h>
#include <vk/games.h>

static const int current_version = 1;

enum {
	MODE_ALTERNATIVE,
	MODE_INTERLEAVE,
	MODE_CONCATENATE
};

vk_buffer_t *
vk_game_get_section_data (vk_game_t *game, const char *name)
{
	unsigned i;
	for (i = 0; i < game->nsections; i++)
		if (!strcmp (game->sections[i].name, name))
			return game->sections[i].buffer;
	return NULL;
}

static vk_buffer_t *
load_datum (json_t *datum, const char *path, const char *game_name)
{
	json_t *name = json_object_get (datum, "name");
	json_t *size = json_object_get (datum, "size");
	const char *datum_name = json_string_value (name);
	unsigned datum_size = json_integer_value (size);
	vk_buffer_t *buffer;
	char full_path[256];

	sprintf (full_path, "%s/%s/%s", path, game_name, datum_name);

	VK_LOG ("Loading %u bytes from '%s'", datum_size, full_path);

	buffer = vk_buffer_new_from_file (full_path, datum_size);

	return buffer;
}

static unsigned
get_section_size (json_t *data, unsigned ndata, unsigned mode)
{
	unsigned i, total_bytes = 0;
	for (i = 0; i < ndata; i++) {
		json_t *datum = json_array_get (data, i);
		json_t *datum_size = json_object_get (datum, "size");
		unsigned nbytes;

		nbytes = json_integer_value (datum_size);
		if (!nbytes)
			return -1;

		switch (mode) {
		case MODE_ALTERNATIVE:
			if (total_bytes == 0)
				total_bytes = nbytes;
			else if (total_bytes != nbytes)
				return 0;
			break;
		case MODE_INTERLEAVE:
		case MODE_CONCATENATE:
			total_bytes += nbytes;
			break;
		}
	}
	return total_bytes;
}

static int
load_section (json_t *root, vk_game_section_t *section, const char *path, const char *game_name)
{
	json_t *name, *type, *endn, *data;
	const char *name_value, *type_value, *endn_value;
	unsigned mode, ndata, i;
	uint32_t total_size, base;

	name = json_object_get (root, "name");
	type = json_object_get (root, "type");
	endn = json_object_get (root, "endn");
	data = json_object_get (root, "data");

	if (!json_is_string (name) ||
	    !json_is_string (type) ||
	    !json_is_string (endn))
		return -1;

	ndata = json_array_size (data);
	if (!ndata)
		return -1;

	name_value = json_string_value (name);
	type_value = json_string_value (type);
	endn_value = json_string_value (endn);

	VK_LOG ("Loading section %s, %s, %s", name_value, type_value, endn_value);

	if (!strcmp (type_value, "alternative"))
		mode = MODE_ALTERNATIVE;
	else if (!strcmp (type_value, "interleave"))
		mode = MODE_INTERLEAVE;
	else if (!strcmp (type_value, "concatenate"))
		mode = MODE_CONCATENATE;
	else
		return -1;

	total_size = get_section_size (data, ndata, mode);
	if (!total_size)
		return -1;

	strcpy (section->name, name_value);

	switch (mode) {
	case MODE_ALTERNATIVE:
		section->buffer = NULL;
		for (i = 0; i < ndata; i++) {
			json_t *datum = json_array_get (data, i);
			section->buffer = load_datum (datum, path, game_name);
			if (section->buffer)
				break;
		}
		if (!section->buffer)
			return -1;
		break;
	case MODE_INTERLEAVE:
		/* XXX wrong, we need to interleave 2 by 2 for hikaru eproms,
		 * see braveff */
		section->buffer = vk_buffer_new (total_size, 0);
		if (!section->buffer)
			return -1;
		base = 0;
		for (i = 0; i < ndata; i++) {
			json_t *datum = json_array_get (data, i);
			json_t *amnt = json_object_get (root, "amnt");
			unsigned nbytes = json_integer_value (amnt), j;
			if (!amnt)
				return -1;
			if (nbytes != 1 && nbytes != 2 && nbytes != 4 && nbytes != 8)
				return -1;
			vk_buffer_t *buf = load_datum (datum, path, game_name);
			if (!buf)
				return -1;
			for (j = 0; j < vk_buffer_get_size (buf); j += nbytes) {
				unsigned k = base + ((i & 1) + j) * nbytes;
				uint64_t bytes = vk_buffer_get (buf, nbytes, j);
				vk_buffer_put (section->buffer, nbytes, k, bytes);
			}
			if (i & 1)
				base += vk_buffer_get_size (buf) * 2;
			vk_buffer_delete (&buf);
		}
		break;
	case MODE_CONCATENATE:
		section->buffer = vk_buffer_new (total_size, 0);
		if (!section->buffer)
			return -1;
		base = 0;
		for (i = 0; i < ndata; i++) {
			json_t *datum = json_array_get (data, i);
			vk_buffer_t *buffer = load_datum (datum, path, game_name);
			unsigned j;
			if (!buffer)
				return -1;
			for (j = 0; j < vk_buffer_get_size (buffer); j += 1) {
				uint8_t byte = vk_buffer_get (buffer, 1, j);
				vk_buffer_put (section->buffer, 1, j+base, byte);
			}
			base += vk_buffer_get_size (buffer);
			vk_buffer_delete (&buffer);
		}
		break;
	}
	return 0;
}

static int
load_sections (json_t *root, vk_game_t *game, const char *path, const char *game_name)
{
	json_t *sections;
	unsigned i, nsections;

	VK_ASSERT (root);
	VK_ASSERT (game);

	sections = json_object_get (root, "sections");

	nsections = json_array_size (sections);
	if (!nsections)
		return -1;

	game->sections = (vk_game_section_t *) calloc (nsections, sizeof (vk_game_section_t));
	if (!game->sections)
		return -1;

	for (i = 0; i < nsections; i++) {
		json_t *section = json_array_get (sections, i);
		if (load_section (section, &game->sections[i], path, game_name))
			return -1;
	}

	game->nsections = nsections;
	return 0;
}

vk_game_t *
vk_game_new (vk_game_list_t *game_list, const char *path, const char *name)
{
	vk_game_t *game;
	unsigned i;

	VK_ASSERT (game_list);
	VK_ASSERT (path);
	VK_ASSERT (name);

	game = ALLOC (vk_game_t);
	if (!game)
		goto fail;

	for (i = 0; i < game_list->nentries; i++)
		if (!strcmp (game_list->entries[i].name, name)) {
			json_t *root = game_list->entries[i].root;
			json_t *required = json_object_get (root, "required");
			strcpy (game->name, name);
			strcpy (game->mach, game_list->entries[i].mach);
			if (load_sections (root, game, path, name))
				goto fail;
		}

	return game;
fail:
	vk_game_delete (&game);
	return NULL;
}

void
vk_game_delete (vk_game_t **game_)
{
	if (game_) {
		vk_game_t *game = *game_;
		unsigned i;
		for (i = 0; i < game->nsections; i++)
			vk_buffer_delete (&game->sections[i].buffer);
		free (game->sections);
		free (game);
	}
}

static int
parse_rom_list (json_t *roms, vk_game_list_t *list)
{
	unsigned i, nentries = json_array_size (roms);

	list->entries = calloc (nentries, sizeof (vk_game_entry_t));
	if (!list->entries)
		return -1;
	list->nentries = nentries;

	for (i = 0; i < nentries; i++) {
		vk_game_entry_t *entry = &list->entries[i];
		json_t *rom = json_array_get (roms, i);
		json_t *name = json_object_get (rom, "name");
		json_t *mach = json_object_get (rom, "mach");

		if (!json_is_string (name) || !json_is_string (mach))
			return -1;

		strncpy (entry->name, json_string_value (name), 31);
		strncpy (entry->mach, json_string_value (mach), 31);
		entry->name[31] = '\0';
		entry->mach[31] = '\0';
		entry->root = rom;

		VK_LOG ("GAME #%u = { '%s', '%s', %p }\n",
		        i, entry->name, entry->mach, entry->root);
	}
	return 0;
}

vk_game_list_t *
vk_game_list_new (const char *path)
{
	vk_game_list_t *list = ALLOC (vk_game_list_t);
	json_t *root, *version, *roms;
	json_error_t error;
	void *text;

	VK_ASSERT (path);

	if (!list)
		return NULL;

	text = vk_load_any (path, NULL);
	if (!text) {
		VK_ERROR ("could not open game list '%s'", path);
		goto fail;
	}

	root = json_loads (text, 0, &error);
	if (!root) {
		VK_ERROR ("can't load game list '%s'", error.text);
		goto fail;
	}
	free (text);

	version = json_object_get (root, "version");
	if (json_is_integer (version)) {
		json_int_t value = json_integer_value (version);
		if (current_version != (unsigned) value) {
			VK_ERROR ("invalid game list version: found %u, expected %u",
			          value, current_version);
			goto fail;
		}
	} else {
		VK_ERROR ("not game list version");
		goto fail;
	}

	roms = json_object_get (root, "roms");
	if (!json_is_array (roms)) {
		VK_ERROR ("invalid game list format");
		goto fail;
	}

	if (parse_rom_list (roms, list)) {
		VK_ERROR ("failed to parse rom list");
		goto fail;
	}

	return list;
fail:
	//vk_game_list_delete (&list);
	return NULL;
}

void
vk_game_list_delete (vk_game_list_t **game_list_)
{
	if (game_list_) {
		vk_game_list_t *game_list = *game_list_;
		free (game_list->entries);
		game_list->entries = NULL;
		FREE (game_list_);
	}
}
