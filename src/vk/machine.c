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

#include "vk/machine.h"

void
vk_machine_destroy (vk_machine_t **mach_)
{
	if (mach_)
		(*mach_)->destroy (mach_);
}

int
vk_machine_load_game (vk_machine_t *mach, vk_game_t *game)
{
	return mach->load_game (mach, game);
}

void
vk_machine_reset (vk_machine_t *mach, vk_reset_type_t type)
{
	mach->reset (mach, type);
}

int
vk_machine_run_frame (vk_machine_t *mach)
{
	return mach->run_frame (mach);
}

int
vk_machine_load_state (vk_machine_t *mach, FILE *fp)
{
	return mach->load_state (mach, fp);
}

int
vk_machine_save_state (vk_machine_t *mach, FILE *fp)
{
	return mach->save_state (mach, fp);
}

const char *
vk_machine_get_debug_string (vk_machine_t *mach)
{
	return mach->get_debug_string (mach);
}
