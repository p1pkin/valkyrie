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
	HR_DEBUG_SELECT_VIEWPORT,
	HR_DEBUG_NO_TEXTURES,
	HR_DEBUG_NO_MIPMAPS,
	HR_DEBUG_SELECT_POLYTYPE,
	HR_DEBUG_NO_INSTANCING,
	HR_DEBUG_SELECT_INSTANCE,
	HR_DEBUG_NO_LIGHTING,
	HR_DEBUG_NO_AMBIENT,
	HR_DEBUG_NO_DIFFUSE,
	HR_DEBUG_NO_SPECULAR,
	HR_DEBUG_NO_FOG,

	HR_NUM_DEBUG_VARS
};

typedef union {
	struct {
		uint32_t has_texture		: 1;
		uint32_t has_lighting		: 1;
		uint32_t has_phong		: 1;

		uint32_t has_light0		: 1;
		uint32_t light0_type		: 2;
		uint32_t light0_att_type	: 3;
		uint32_t has_light0_specular	: 1;

		uint32_t has_light1		: 1;
		uint32_t light1_type		: 2;
		uint32_t light1_att_type	: 3;
		uint32_t has_light1_specular	: 1;

		uint32_t has_light2		: 1;
		uint32_t light2_type		: 2;
		uint32_t light2_att_type	: 3;
		uint32_t has_light2_specular	: 1;

		uint32_t has_light3		: 1;
		uint32_t light3_type		: 2;
		uint32_t light3_att_type	: 3;
		uint32_t has_light3_specular	: 1;

		uint32_t has_fog		: 1;
	};
	uint32_t full;
} hikaru_glsl_variant_t;

typedef struct {
	GLuint id;
	hikaru_texhead_t th;
} hikaru_texture_t;

typedef struct {
	GLuint			vbo;
	uint32_t		num_tris;
	uint32_t		addr[2];
	uint32_t		vp_index;
	uint32_t		mv_index;
	uint32_t		num_instances;
	uint32_t		mat_index;
	uint32_t		tex_index;
	uint32_t		ls_index;
	float			alpha_thresh[2];
	uint32_t		num;
} hikaru_mesh_t;

typedef struct {
	vk_renderer_t base;

	hikaru_gpu_t *gpu;

	hikaru_viewport_t	*vp_list;
	uint32_t		 num_vps;

	hikaru_modelview_t	*mv_list;
	uint32_t		 num_mvs;
	uint32_t		 num_instances;

	hikaru_material_t	*mat_list;
	uint32_t		 num_mats;

	hikaru_texhead_t	*tex_list;
	uint32_t		 num_texs;

	hikaru_lightset_t	*ls_list;
	uint32_t		 num_lss;

	hikaru_mesh_t		*mesh_list[8][8];
	uint32_t		 num_meshes[8][8];
	uint32_t		 total_meshes;

	struct {
		unsigned		num_verts, num_tris;
		hikaru_vertex_t		tmp[4];
		hikaru_vertex_body_t	all[MAX_VERTICES_PER_MESH];
	} push;

	struct {
		hikaru_mesh_t		*current;

		hikaru_glsl_variant_t	variant;
		GLuint			program;
		GLuint			vao;

		struct {
			GLuint		u_projection;
			GLuint		u_modelview;
			GLuint		u_normal;
			struct {
				GLuint	position;
				GLuint	direction;
				GLuint	diffuse;
				GLuint	specular;
				GLuint	extents;
			} u_lights[4];
			GLuint		u_ambient;
			GLuint		u_texture;
			GLuint		u_fog;
			GLuint		u_fog_color;
		} locs;
	} meshes;

	struct {
		hikaru_texture_t cache[2][0x40][0x80];
		bool is_clear[2];
	} textures;

	struct {
		GLuint program, vao, vbo;
		struct {
			GLuint u_projection;
			GLuint u_texture;
			GLuint u_texture_multiplier;
		} locs;
	} layers;

	struct {
		int32_t flags[HR_NUM_DEBUG_VARS];
	} debug;

} hikaru_renderer_t;

#define LOG(fmt_, args_...) \
	do { \
		if (hr->debug.flags[HR_DEBUG_LOG]) \
			fprintf (stdout, "\tHR: " fmt_"\n", ##args_); \
	} while (0)

uint32_t	rgba1111_to_rgba4444 (uint8_t pixel);
uint16_t	abgr1555_to_rgba5551 (uint16_t pixel);
uint16_t	abgr4444_to_rgba4444 (uint16_t pixel);
uint32_t	a8_to_rgba8888 (uint32_t a);

#endif /* __HIKARU_RENDERER_PRIVATE_H__ */

