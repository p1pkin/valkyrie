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

#define MAX_VERTICES_PER_MESH	16384

/* Used for both texheads and layers */
enum {
	HIKARU_FORMAT_ABGR1555 = 0,
	HIKARU_FORMAT_ABGR4444 = 1,
	HIKARU_FORMAT_ABGR1111 = 2,
	HIKARU_FORMAT_ALPHA8 = 4,
	HIKARU_FORMAT_ABGR8888 = 8,
};

#define HIKARU_GPU_OBJ_SET	(1 << 0)
#define HIKARU_GPU_OBJ_DIRTY	(1 << 1)

typedef struct {
	uint32_t flags;

	struct {
		float l, r;
		float b, t;
		float f, n;
	} clip;

	struct {
		float x, y;
	} offset;

	struct {
		float max, min;
		float density, bias;
		vec4b_t mask;
		uint32_t func		: 3;
		uint32_t q_type		: 2;
		uint32_t q_enabled	: 1;
		uint32_t q_unknown	: 1;
	} depth;

	struct {
		vec4b_t clear;
		vec3s_t ambient;
	} color;
} hikaru_gpu_viewport_t;

typedef struct {
	mtx4x4f_t mtx;
	uint32_t set		: 1;
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
	uint32_t _2C1_unk4	: 3;	/* 2C1 */
	uint32_t _2C1_unk8	: 3;	/* 2C1 */
	uint32_t bank		: 1;	/* 4C1 */
	uint32_t has_mipmap	: 1;
	uint32_t set		: 1;
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
} hikaru_gpu_lightset_t;

typedef struct {
	uint32_t x0, y0, x1, y1;
	uint32_t format		: 4;
	uint32_t enabled	: 1;
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
	uint16_t unk_00400000;

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
		hikaru_gpu_modelview_t table[NUM_MODELVIEWS];
		uint32_t depth, total;
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
		hikaru_gpu_layer_t layer[2][2];
		bool enabled;
	} layers;

	struct {
		bool log_dma;
		bool log_idma;
		bool log_cp;
	} options;

} hikaru_gpu_t;

#define REG15(addr_)	(*(uint32_t *) &gpu->regs_15[(addr_) & 0xFF])
#define REG18(addr_)	(*(uint32_t *) &gpu->regs_18[(addr_) & 0xFF])
#define REG1A(addr_)	(*(uint32_t *) &gpu->regs_1A[(addr_) & 0x1FF])
#define REG1AUNIT(n,a)	(*(uint32_t *) &gpu->regs_1A_unit[n][(a) & 0x3F])
#define REG1AFIFO(a)	(*(uint32_t *) &gpu->regs_1A_fifo[(a) & 0xF])

/****************************************************************************
 Renderer
****************************************************************************/

#define HR_PUSH_POS	(1 << 0)
#define HR_PUSH_NRM	(1 << 1)
#define HR_PUSH_TXC	(1 << 2)

typedef union {
	struct {
		uint32_t winding	: 1;
		uint32_t ppivot		: 1;
		uint32_t tpivot		: 1;
		uint32_t padding1	: 6;
		uint32_t tricap		: 3;
		uint32_t unknown1	: 1;
		uint32_t unknown2	: 3;
		uint32_t padding2	: 7;
		uint32_t unknown3	: 1;
		uint32_t alpha		: 8;
	} bit;
	uint32_t full;
} hikaru_gpu_vertex_info_t;

typedef struct hikaru_gpu_vertex_t hikaru_gpu_vertex_t;

struct hikaru_gpu_vertex_t {
	hikaru_gpu_vertex_info_t info;
	vec3f_t	pos;
	uint32_t padding0;
	vec3f_t	nrm;
	uint32_t padding1;
	vec4f_t col;
	vec2f_t	txc;
	vec2f_t padding2;
} __attribute__ ((packed));

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
		uint32_t flags;
		uint32_t current_mesh, selected_mesh;
	} debug;

} hikaru_renderer_t;

/****************************************************************************
 Definitions
****************************************************************************/

typedef enum {
	_15_IRQ_IDMA	= (1 << 0),
	_15_IRQ_VBLANK	= (1 << 1),
	_15_IRQ_DONE	= (1 << 2),
	_15_IRQ_UNK3	= (1 << 3),
	_15_IRQ_UNK4	= (1 << 4),
	_15_IRQ_UNK5	= (1 << 5),
	_15_IRQ_UNK6	= (1 << 6),
	_15_IRQ_1A	= (1 << 7)
} _15_irq_t;

typedef enum {
	_1A_IRQ_UNK0	= (1 << 0),
	_1A_IRQ_VBLANK	= (1 << 1),
	_1A_IRQ_DONE	= (1 << 2),
	_1A_IRQ_UNK3	= (1 << 3)
} _1a_irq_t;

#define ispositive(x_) \
	(isfinite(x_) && (x_) >= 0.0)

/* hikaru-gpu-private.c */
void slot_to_coords (uint32_t *, uint32_t *, uint32_t, uint32_t);
const char *get_gpu_viewport_str (hikaru_gpu_viewport_t *);
const char *get_gpu_modelview_str (hikaru_gpu_modelview_t *);
const char *get_gpu_material_str (hikaru_gpu_material_t *);
const char *get_gpu_texhead_str (hikaru_gpu_texhead_t *);
const char *get_gpu_light_str (hikaru_gpu_light_t *);
const char *get_gpu_vertex_str (hikaru_gpu_vertex_t *);
const char *get_gpu_layer_str (hikaru_gpu_layer_t *);

/* hikaru-gpu.c */
void hikaru_gpu_raise_irq (hikaru_gpu_t *gpu, uint32_t _15, uint32_t _1A);

/* hikaru-gpu-cp.c */
void hikaru_gpu_cp_init (hikaru_gpu_t *);
void hikaru_gpu_cp_exec (hikaru_gpu_t *, int cycles);
void hikaru_gpu_cp_vblank_in (hikaru_gpu_t *);
void hikaru_gpu_cp_vblank_out (hikaru_gpu_t *);
void hikaru_gpu_cp_on_put (hikaru_gpu_t *);

/* hikaru-renderer.c */
void hikaru_renderer_begin_mesh (hikaru_renderer_t *hr, uint32_t addr,
                                 bool is_static);
void hikaru_renderer_end_mesh (hikaru_renderer_t *hr, uint32_t addr);
void hikaru_renderer_push_vertices (hikaru_renderer_t *hr,
                                    hikaru_gpu_vertex_t *v,
                                    uint32_t push,
                                    unsigned num);

#endif /* __HIKARU_GPU_PRIVATE_H__ */
