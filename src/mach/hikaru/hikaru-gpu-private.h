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

#ifndef __HIKARU_GPU_PRIVATE_H__
#define __HIKARU_GPU_PRIVATE_H__

#include <math.h>

#include "vk/device.h"
#include "vk/surface.h"

#define NUM_VIEWPORTS	8
#define NUM_MODELVIEWS	256
#define NUM_MATERIALS	16384 /* XXX bogus */
#define NUM_TEXHEADS	16384 /* XXX bogus */
#define NUM_LIGHTS	1024
#define NUM_LIGHTSETS	200

#define MAX_VERTICES_PER_MESH	4096

/* Used for both texheads and layers */
enum {
	HIKARU_FORMAT_RGBA5551 = 0,
	HIKARU_FORMAT_RGBA4444 = 1,
	HIKARU_FORMAT_RGBA1111 = 2,
	HIKARU_FORMAT_ALPHA8 = 4,
	HIKARU_FORMAT_RGBA8888 = 8,
};

typedef struct {
	float persp_zfar;		/* 021 */
	float persp_znear;		/* 021 */
	float depth_near;		/* 421 */
	float depth_far;		/* 421 */
	float depthq_density;		/* 621 */
	float depthq_bias;		/* 621 */
	vec2s_t center;			/* 221 */
	vec2s_t extents_x;		/* 221 */
	vec2s_t extents_y;		/* 221 */
	vec4b_t	depthq_mask;		/* 621 */
	vec4b_t clear_color;		/* 991 */
	vec3s_t ambient_color;		/* 881 */
	uint32_t depth_func	: 3;	/* 421 */
	uint32_t depthq_type	: 2;	/* 621 */
	uint32_t depthq_enabled	: 1;	/* 621 */
	uint32_t depthq_unk	: 1;	/* 621 */
	uint32_t set		: 1;
	uint32_t uploaded	: 1;
} hikaru_gpu_viewport_t;

typedef struct {
	mtx4x4f_t mtx;
	uint32_t set		: 1;
	uint32_t uploaded	: 1;
} hikaru_gpu_modelview_t;

typedef struct {
	vec3b_t color[2];		/* 091, 291 */
	vec3s_t material_color;		/* 691 */
	vec3b_t shininess;		/* 491 */
	uint8_t specularity;		/* 491 */
	uint32_t shading_mode	: 2;	/* 881 */
	uint32_t depth_blend	: 1;	/* 881 */
	uint32_t has_texture	: 1;	/* 881 */
	uint32_t has_alpha	: 1;	/* 881 */
	uint32_t has_highlight	: 1;	/* 881 */
	uint32_t blending_mode	: 2;	/* A81 */
	uint32_t set		: 1;
	uint32_t uploaded	: 1;
} hikaru_gpu_material_t;

typedef struct {
	uint32_t bus_addr;		/* IDMA */
	uint32_t size;			/* IDMA */
	uint32_t width;			/* 2C1 */
	uint32_t height;		/* 2C1 */
	uint8_t slotx;			/* 4C1 */
	uint8_t sloty;			/* 4C1 */
	uint8_t _0C1_byte;		/* 0C1 */
	uint32_t _0C1_nibble	: 4;	/* 0C1 */
	uint32_t format		: 3;	/* 2C1 */
	uint32_t log_width	: 3;	/* 2C1 */
	uint32_t log_height	: 3;	/* 2C1 */
	uint32_t _2C1_unk4	: 3;	/* 2C1 */
	uint32_t _2C1_unk8	: 3;	/* 2C1 */
	uint32_t bank		: 1;	/* 4C1 */
	uint32_t has_mipmap	: 1;
	uint32_t set		: 1;
	uint32_t uploaded	: 1;
} hikaru_gpu_texhead_t;

typedef struct {
	/* 261 */
	uint32_t emission_type	: 2;
	float emission_p;
	float emission_q;
	/* 961 */
	vec3f_t position;
	/* B61 */
	vec3f_t direction;
	/* 051 */
	uint32_t _051_index	: 8; /* XXX review me */
	vec3s_t _051_color;
	uint32_t set		: 1;
} hikaru_gpu_light_t;

typedef struct {
	hikaru_gpu_light_t *lights[4];
	uint32_t set		: 1;
	uint32_t uploaded	: 1;
} hikaru_gpu_lightset_t;

typedef struct {
	uint32_t is_static	: 1;
	uint32_t has_position	: 1;
	uint32_t has_normal	: 1;
	uint32_t has_texcoords	: 1;

	/* Only meaningful if has_position */
	uint32_t has_pvu_mask	: 1;
	uint32_t pvu_mask	: 3;
	uint32_t ppivot		: 1;

	/* Only meaningful if has_texcoords */
	uint32_t tpivot		: 1;

	/* Only meaningful if has_position || has_texcoords */
	uint32_t winding	: 1;
} hikaru_gpu_vertex_info_t;

typedef struct hikaru_gpu_vertex_t hikaru_gpu_vertex_t;

struct hikaru_gpu_vertex_t {
	vec3f_t	pos;
	uint32_t padding0;
	vec3f_t	nrm;
	uint32_t padding1;
	vec3f_t col;
	float alpha;
	vec2f_t	txc;
	vec2f_t padding2;
} __attribute__ ((packed));

typedef struct {
	uint32_t x0, y0, x1, y1;
	unsigned format;
} hikaru_gpu_layer_t;

typedef struct {
	vk_device_t base;

	vk_buffer_t *cmdram;
	vk_buffer_t *texram[2];
	vk_buffer_t *fb;

	vk_renderer_t *renderer;

	uint8_t regs_15[0x100];
	uint8_t regs_18[0x100];
	uint8_t regs_1A[0x104];
	uint8_t regs_1A_unit[2][0x40];
	uint8_t regs_1A_fifo[0x10];

	unsigned frame_type;

	/* CS Execution State */

	struct {
		uint32_t pc, sp[2];
		bool is_running;
		bool unhandled;
	} cp;

	/* Rendering State */
	bool in_mesh;
	float static_mesh_precision;

	struct {
		hikaru_gpu_viewport_t table[NUM_VIEWPORTS];
		hikaru_gpu_viewport_t scratch;
	} viewports;

	struct {
		hikaru_gpu_modelview_t stack[NUM_MODELVIEWS];
		uint32_t depth;
	} modelviews;

	struct {
		hikaru_gpu_material_t table[NUM_MATERIALS];
		hikaru_gpu_material_t scratch;
		uint32_t base;
	} materials;

	struct {
		hikaru_gpu_texhead_t table[NUM_TEXHEADS];
		hikaru_gpu_texhead_t scratch;
		uint32_t base;
	} texheads;

	struct {
		hikaru_gpu_lightset_t sets[NUM_LIGHTSETS];
		hikaru_gpu_light_t table[NUM_LIGHTS];
		hikaru_gpu_light_t scratch;
		uint32_t base;
	} lights;

	struct {
		bool log_dma;
		bool log_idma;
		bool log_cp;
	} options;

} hikaru_gpu_t;

typedef struct {
	vk_renderer_t base;

	hikaru_gpu_t *gpu;

	struct {
		hikaru_gpu_vertex_t	vbo[MAX_VERTICES_PER_MESH];
		uint16_t		ibo[MAX_VERTICES_PER_MESH];
		uint16_t		iindex, vindex;
		uint16_t		ppivot, tpivot;
	} mesh;

	struct {
		vk_surface_t *fb, *texram, *debug, *current;
	} textures;

	struct {
		bool log;
		bool disable_2d;
		bool disable_3d;
		bool draw_fb;
		bool draw_texram;
		bool force_debug_texture;
	} options;

} hikaru_renderer_t;

/* hikaru-gpu-private.c */
void slot_to_coords (uint32_t *, uint32_t *, uint32_t, uint32_t);
const char *get_gpu_viewport_str (hikaru_gpu_viewport_t *);
const char *get_gpu_modelview_str (hikaru_gpu_modelview_t *);
const char *get_gpu_material_str (hikaru_gpu_material_t *);
const char *get_gpu_texhead_str (hikaru_gpu_texhead_t *);
const char *get_gpu_light_str (hikaru_gpu_light_t *);
const char *get_gpu_vertex_str (hikaru_gpu_vertex_t *v,
                                hikaru_gpu_vertex_info_t *vi);
const char *get_gpu_layer_str (hikaru_gpu_layer_t *);

/* hikaru-gpu-insns.c */
void hikaru_gpu_cp_init (hikaru_gpu_t *);
void hikaru_gpu_cp_end_processing (hikaru_gpu_t *gpu);

/* hikaru-renderer.c */
void hikaru_renderer_draw_layer (vk_renderer_t *renderer,
                                 hikaru_gpu_layer_t *layer);
void hikaru_renderer_begin_mesh (hikaru_renderer_t *hr, bool is_static);
void hikaru_renderer_end_mesh (hikaru_renderer_t *hr);
void hikaru_renderer_push_vertices (hikaru_renderer_t *hr,
                                    hikaru_gpu_vertex_t *v,
                                    hikaru_gpu_vertex_info_t *vi,
                                    unsigned num);

#endif /* __HIKARU_GPU_PRIVATE_H__ */
