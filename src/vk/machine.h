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

#ifndef __VK_MACH_H__
#define __VK_MACH_H__

#include "vk/core.h"
#include "vk/games.h"
#include "vk/renderer.h"

typedef enum {
	VK_RESET_TYPE_HARD,
	VK_RESET_TYPE_SOFT,

	VK_NUM_RESET_TYPES
} vk_reset_type_t;

typedef struct vk_machine_t vk_machine_t;

struct vk_machine_t {
	char name[64];

	vk_game_t	*game;
	vk_renderer_t	*renderer;

	void		 (* destroy)(vk_machine_t **mach_);
	int		 (* load_game) (vk_machine_t *mach, vk_game_t *game);
	void		 (* reset) (vk_machine_t *mach, vk_reset_type_t type);
	int		 (* run_frame) (vk_machine_t *mach);
	int		 (* load_state) (vk_machine_t *mach, FILE *fp);
	int		 (* save_state) (vk_machine_t *mach, FILE *fp);
	const char	*(* get_debug_string)(vk_machine_t *mach);
};

#define VK_MACH_LOG(mach_, fmt_, args_...) \
	do { \
		const char *str = vk_machine_get_debug_string (mach_); \
		VK_LOG ("%s : "fmt_, str, ##args_); \
	} while (0);

#define VK_MACH_ERROR(mach_, fmt_, args_...) \
	do { \
		const char *str = vk_machine_get_debug_string (mach_); \
		VK_ERROR ("%s : "fmt_, str, ##args_); \
	} while (0);

#define VK_MACH_ABORT(mach_, fmt_, args_...) \
	do { \
		const char *str = vk_machine_get_debug_string (mach_); \
		VK_ABORT ("%s : "fmt_, str, ##args_); \
	} while (0);

#define VK_MACH_ASSERT(mach_, cond_) \
	VK_ASSERT (cond_)

static inline void
vk_machine_destroy (vk_machine_t **mach_)
{
	if (mach_)
		(*mach_)->destroy (mach_);
}

static inline int
vk_machine_load_game (vk_machine_t *mach, vk_game_t *game)
{
	return mach->load_game (mach, game);
}

static inline void
vk_machine_reset (vk_machine_t *mach, vk_reset_type_t type)
{
	mach->reset (mach, type);
}

static inline int
vk_machine_run_frame (vk_machine_t *mach)
{
	return mach->run_frame (mach);
}

static inline int
vk_machine_load_state (vk_machine_t *mach, FILE *fp)
{
	return mach->load_state (mach, fp);
}

static inline int
vk_machine_save_state (vk_machine_t *mach, FILE *fp)
{
	return mach->save_state (mach, fp);
}

static inline const char *
vk_machine_get_debug_string (vk_machine_t *mach)
{
	return mach->get_debug_string (mach);
}

#endif /* __VK_MACH_H__ */
