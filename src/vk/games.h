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

#ifndef __VK_GAMES_H__
#define __VK_GAMES_H__

#include <jansson.h>

#include "vk/buffer.h"

typedef struct {
	char name[32];
	vk_buffer_t *buffer;
} vk_game_section_t;

typedef struct {
	char name[32];
	char mach[32];
	vk_game_section_t *sections;
	unsigned nsections;
} vk_game_t;

typedef struct {
	char name[32];
	char mach[32];
	json_t *root;
} vk_game_entry_t;

typedef struct {
	vk_game_entry_t *entries;
	unsigned nentries;
} vk_game_list_t;

vk_game_t	*vk_game_new (vk_game_list_t *list, const char *path, const char *name);
void		 vk_game_delete (vk_game_t **game_);
vk_buffer_t	*vk_game_get_section_data (vk_game_t *game, const char *name);

vk_game_list_t	*vk_game_list_new (const char *path);
void		 vk_game_list_delete (vk_game_list_t **game_list_);

#endif /* __VK_GAMES_H __ */
