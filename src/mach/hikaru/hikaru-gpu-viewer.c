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

#include <unistd.h>

#include "vk/core.h"
#include "vk/input.h"
#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-gpu-private.h"
#include "mach/hikaru/hikaru-renderer.h"

static bool
process_events (void)
{
	SDL_Event event;
	bool quit = false;

	while (SDL_PollEvent (&event)) {
		switch (event.type) {
		case SDL_KEYDOWN:
			vk_input_set_key (event.key.keysym.sym, true);
			switch (event.key.keysym.sym) {
			case SDLK_ESCAPE:
				quit = true;
				break;
			default:
				break;
			}
			break;
		case SDL_KEYUP:
			vk_input_set_key (event.key.keysym.sym, false);
			break;
		case SDL_QUIT:
			quit = true;
			break;
		}
	}
	return quit;
}

int
main (int argc, char **argv)
{
	char str[256], *game = NULL;
	vk_machine_t *mach;
	hikaru_t *hikaru;
	int num_frames = -1, count;

	if (argc > 1)
		game = argv[1];

	if (!game) {
		printf ("Usage: %s game [--frames|-n]\n", argv[0]);
		return -1;
	}

	if (argc > 2) {
		if (!strcmp (argv[2], "--frames") ||
		    !strcmp (argv[2], "-n"))
			num_frames = atoi (argv[3]);
	}

	mach = hikaru_new (NULL);
	hikaru = (hikaru_t *) mach;

	vk_machine_reset (mach, VK_RESET_TYPE_HARD);

	vk_buffer_destroy (&hikaru->cmdram);
	sprintf (str, "%s-cmdram.bin", game);
	hikaru->cmdram    = vk_buffer_new_from_file (str, 4*MB);

	vk_buffer_destroy (&hikaru->texram[0]);
	sprintf (str, "%s-texram-0.bin", game);
	hikaru->texram[0] = vk_buffer_new_from_file (str, 4*MB);

	vk_buffer_destroy (&hikaru->texram[1]);
	sprintf (str, "%s-texram-1.bin", game);
	hikaru->texram[1] = vk_buffer_new_from_file (str, 4*MB);

	vk_buffer_destroy (&hikaru->ram_s);
	sprintf (str, "%s-ram-s.bin", game);
	hikaru->ram_s = vk_buffer_new_from_file (str, 32*MB);

	count = 0;
	while (!process_events ()) {
		hikaru_gpu_t *gpu = (hikaru_gpu_t *) hikaru->gpu;

		gpu->cp.is_running = true;
		gpu->cp.pc = 0x48000100;
		gpu->cp.sp[0] = 0x48020000;
		gpu->cp.sp[1] = 0x48020000;
		*(uint32_t *) &gpu->regs_15[0x58] = 3;

		vk_renderer_begin_frame (mach->renderer);
		vk_device_exec (hikaru->gpu, 4*1000*1000);
		vk_renderer_end_frame (mach->renderer);

		if (++count == num_frames)
			break;

		usleep (500*1000);
	}

	vk_machine_destroy (&mach);

	return 0;
}
