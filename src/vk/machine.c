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
	if (mach_) {
		vk_machine_t *mach = *mach_;

		vk_vector_destroy (&mach->buffers);
		vk_vector_destroy (&mach->devices);
		vk_vector_destroy (&mach->cpus);

		mach->destroy (mach_);
	}
}

static void
register_component (vk_vector_t **where_, void *what, unsigned num_default)
{
	void **entry;

	VK_ASSERT (where_);
	VK_ASSERT (what);
	VK_ASSERT (num_default);

	if (!*where_) {
		*where_ = vk_vector_new (num_default, sizeof (void *));
		VK_ASSERT (*where_);
	}

	entry = (void **) vk_vector_append_entry (*where_);
	VK_ASSERT (entry);

	*entry = what;
}

void
vk_machine_register_buffer (vk_machine_t *mach, void *ptr)
{
	register_component (&mach->buffers, ptr, 16);
	VK_LOG ("machine: registered buffer %p, size=%08X",
	        ptr, vk_buffer_get_size ((vk_buffer_t *) ptr));
}

void
vk_machine_register_device (vk_machine_t *mach, void *ptr)
{
	register_component (&mach->devices, ptr, 16);
	VK_LOG ("machine: registered device %p", ptr);
}

void
vk_machine_register_cpu (vk_machine_t *mach, void *ptr)
{
	register_component (&mach->cpus, ptr, 4);
	VK_LOG ("machine: registered cpu %p", ptr);
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
