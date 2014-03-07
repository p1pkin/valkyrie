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

#ifndef __HIKARU_RENDERER_PRIVATE_H__
#define __HIKARU_RENDERER_PRIVATE_H__

#include "mach/hikaru/hikaru-gpu-private.h"

enum {
	HR_DEBUG_LOG,
	HR_DEBUG_NO_LAYER1,
	HR_DEBUG_NO_LAYER2,
	HR_DEBUG_NO_3D,
	HR_DEBUG_SELECT_CULLFACE,
	HR_DEBUG_SELECT_BASE_COLOR,
	HR_DEBUG_NO_TEXTURES,
	HR_DEBUG_USE_DEBUG_TEXTURE,
	HR_DEBUG_DUMP_TEXTURES,
	HR_DEBUG_SELECT_POLYTYPE,
	HR_DEBUG_DRAW_NORMALS,
	HR_DEBUG_NO_LIGHTING,
	HR_DEBUG_NO_AMBIENT,
	HR_DEBUG_NO_DIFFUSE,
	HR_DEBUG_NO_SPECULAR,
	HR_DEBUG_SELECT_ATT_TYPE,

	HR_NUM_DEBUG_VARS
};

typedef struct {
	vk_renderer_t base;

	hikaru_gpu_t *gpu;

	struct {
		hikaru_gpu_vertex_t	vtx[4];
		hikaru_gpu_vertex_t	vbo[MAX_VERTICES_PER_MESH];
		uint32_t		num_pushed;
		uint32_t		num_tris;
		uint32_t		addr[2];
	} mesh;

	struct {
		vk_surface_t *debug;
	} textures;

	struct {
		int32_t flags[HR_NUM_DEBUG_VARS];
	} debug;

} hikaru_renderer_t;

#define LOG(fmt_, args_...) \
	do { \
		if (hr->debug.flags[HR_DEBUG_LOG]) \
			fprintf (stdout, "\tHR: " fmt_"\n", ##args_); \
	} while (0)

void		hikaru_renderer_draw_layers (hikaru_renderer_t *hr,
		                             bool background);

uint32_t	rgba1111_to_rgba4444 (uint8_t pixel);
uint16_t	abgr1555_to_rgba5551 (uint16_t pixel);
uint16_t	abgr4444_to_rgba4444 (uint16_t pixel);
uint32_t	a8_to_rgba8888 (uint32_t a);

#endif /* __HIKARU_RENDERER_PRIVATE_H__ */

