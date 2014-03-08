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

#include "vk/device.h"
#include "vk/surface.h"

#define NUM_VIEWPORTS	8
#define NUM_MODELVIEWS	256
#define NUM_MATERIALS	16384
#define NUM_TEXHEADS	16384
#define NUM_LIGHTS	1024
#define NUM_LIGHTSETS	256

#define MAX_VERTICES_PER_MESH	16384

/* Used for both texheads and layers */
enum {
	HIKARU_FORMAT_ABGR1555 = 0,
	HIKARU_FORMAT_ABGR4444 = 1,
	HIKARU_FORMAT_ABGR1111 = 2,
	HIKARU_FORMAT_ALPHA8 = 4,
	HIKARU_FORMAT_ABGR8888 = 8,
};

/* PHARRIER lists the following polygon types:
 *
 * opaque, shadow A, shadow B, transparent, background, translucent.
 *
 * Each of them is associated to a GPU HW performance counter, so the hardware
 * very likely distinguishes between them too. I can't be sure, but the
 * enumeration below looks so fitting...
 */

enum {
	HIKARU_POLYTYPE_OPAQUE = 1,
	HIKARU_POLYTYPE_SHADOW_A = 2,
	HIKARU_POLYTYPE_SHADOW_B = 3,
	HIKARU_POLYTYPE_TRANSPARENT = 4, /* DreamZzz calls it punchthrough */
	HIKARU_POLYTYPE_BACKGROUND = 5,
	HIKARU_POLYTYPE_TRANSLUCENT = 6
};

typedef struct {
	struct {
		float l, r;
		float b, t;
		float f, n, f2;
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

	uint32_t set	: 1;
	uint32_t dirty	: 1;
} hikaru_gpu_viewport_t;

typedef struct {
	mtx4x4f_t mtx;
} hikaru_gpu_modelview_t;

typedef struct {
	vec4b_t diffuse;
	vec3b_t ambient;
	vec4b_t specular;
	vec3s_t unknown;
	union {
		struct {
			uint32_t		: 12;
			uint32_t unk1		: 1;
			uint32_t		: 3;
			uint32_t unk2		: 4;
			uint32_t		: 12;
		};
		uint32_t full;
	} _081;
	union {
		struct {
			uint32_t		: 16;
			uint32_t shading_mode	: 2;
			uint32_t depth_blend	: 1;
			uint32_t has_texture	: 1;
			uint32_t has_alpha	: 1;
			uint32_t has_highlight	: 1;
			uint32_t unk1		: 1;
			uint32_t unk2		: 1;
			uint32_t unk3		: 8;
		};
		uint32_t full;
	} _881;
	union {
		struct {
			uint32_t		: 16;
			uint32_t blending_mode	: 2;
			uint32_t		: 14;
		};
		uint32_t full;
	} _A81;
	union {
		struct {
			uint32_t		: 16;
			uint32_t alpha_index	: 6;
			uint32_t		: 10;
		};
		uint32_t full;
	} _C81;
	uint32_t set	: 1;
	uint32_t dirty	: 1;
} hikaru_gpu_material_t;

typedef struct {
	union {
		struct {
			uint32_t 	: 16;
			uint32_t unk1	: 2;
			uint32_t 	: 2;
			uint32_t unk2	: 8;
			uint32_t 	: 4;
		};
		uint32_t full;
	} _0C1;
	union {
		struct {
			uint32_t	: 14;
			uint32_t unk1	: 2;
			uint32_t logw	: 3;
			uint32_t logh	: 3;
			uint32_t wrapu	: 1;
			uint32_t wrapv	: 1;
			uint32_t repeatu: 1;
			uint32_t repeatv: 1;
			uint32_t format	: 3;
			uint32_t unk2	: 3;
		};
		uint32_t full;
	} _2C1;
	union {
		struct {
			uint32_t	: 12;
			uint32_t bank	: 1;
			uint32_t	: 3;
			uint32_t slotx	: 8;
			uint32_t sloty	: 8;
		};
		uint32_t full;
	} _4C1;
	uint32_t width;
	uint32_t height;
	uint32_t has_mipmap	: 1;
	uint32_t set		: 1;
	uint32_t dirty		: 1;
} hikaru_gpu_texhead_t;

typedef struct {
	vec3f_t pos;
	vec3f_t dir;
	vec3s_t diffuse;
	vec3b_t specular;
	float att_base;
	float att_offs;
	uint32_t type		: 2;
	uint32_t has_pos	: 1;
	uint32_t has_dir	: 1;
	uint32_t _051_index	: 4;
	uint32_t _051_bit	: 1;
	uint32_t _451_enabled	: 1;
	uint32_t set		: 1;
	uint32_t dirty		: 1;
} hikaru_gpu_light_t;

typedef struct {
	uint16_t index[4];
	uint32_t disabled	: 4;
	uint32_t set		: 1;
	uint32_t dirty		: 1;
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

	struct {
		uint8_t _15[0x100];
		uint8_t _18[0x100];
		uint8_t _1A[0x104];
		uint8_t _1A_unit[2][0x40];
		uint8_t _1A_dma[0x10];
		uint16_t _00400000;
	} regs;

	struct {
		uint32_t pc;
		uint32_t sp[2];
		uint32_t is_running	: 1;
		uint32_t is_unhandled	: 1;
	} cp;

	struct {
		uint32_t in_mesh	: 1;

		struct {
			uint32_t type	: 3;
			float alpha;
			float static_mesh_precision;
		} poly;

		struct {
			hikaru_gpu_viewport_t table[NUM_VIEWPORTS];
			hikaru_gpu_viewport_t scratch;
			hikaru_gpu_viewport_t stack[32];
			int32_t depth;
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
			hikaru_gpu_lightset_t scratchset;
			hikaru_gpu_light_t table[NUM_LIGHTS];
			hikaru_gpu_light_t scratch;
			uint32_t base;
		} lights;

		union {
			struct {
				uint32_t lo : 8;
				uint32_t hi : 24;
			} part;
			uint32_t full;
		} alpha_table[0x40];

		union {
			struct {
				uint32_t hi : 16;
				uint32_t lo : 16;
			};
			uint32_t full;
		} light_table[4][0x20];

		struct {
			hikaru_gpu_layer_t layer[2][2];
			bool enabled;
		} layers;

	} state;

	struct {
		uint32_t log_dma	: 1;
		uint32_t log_idma	: 1;
		uint32_t log_cp		: 1;
	} debug;

} hikaru_gpu_t;

#define REG15(addr_)	(*(uint32_t *) &gpu->regs._15[(addr_) & 0xFF])
#define REG18(addr_)	(*(uint32_t *) &gpu->regs._18[(addr_) & 0xFF])
#define REG1A(addr_)	(*(uint32_t *) &gpu->regs._1A[(addr_) & 0x1FF])
#define REG1AUNIT(n,a)	(*(uint32_t *) &gpu->regs._1A_unit[n][(a) & 0x3F])
#define REG1ADMA(a)	(*(uint32_t *) &gpu->regs._1A_dma[(a) & 0xF])

#define PC		gpu->cp.pc
#define SP(i_)		gpu->cp.sp[i_]
#define UNHANDLED	gpu->cp.is_unhandled

#define POLY		gpu->state.poly
#define VP		gpu->state.viewports
#define MV		gpu->state.modelviews
#define MAT		gpu->state.materials
#define TEX		gpu->state.texheads
#define LIT		gpu->state.lights
#define ATABLE		gpu->state.alpha_table
#define LTABLE		gpu->state.light_table
#define LAYERS		gpu->state.layers

/****************************************************************************
 Renderer
****************************************************************************/

#define HR_PUSH_POS	(1 << 0)
#define HR_PUSH_NRM	(1 << 1)
#define HR_PUSH_TXC	(1 << 2)

typedef union {
	struct {
		uint32_t winding	: 1; /* 0x00000001 */
		uint32_t ppivot		: 1; /* 0x00000002 */
		uint32_t tpivot		: 1; /* 0x00000004 */
		uint32_t padding1	: 6;
		uint32_t tricap		: 3; /* 0x00000E00 */
		uint32_t unknown1	: 1; /* 0x00001000 */
		uint32_t unknown2	: 3; /* 0x0000E000 */
		uint32_t padding2	: 3;
		uint32_t unknown3	: 1; /* 0x00080000 */
		uint32_t padding3	: 3;
		uint32_t unknown4	: 1; /* 0x00800000 */
		uint32_t alpha		: 8; /* 0xFF000000 */
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
void get_texhead_coords (uint32_t *, uint32_t *, hikaru_gpu_texhead_t *);
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
void hikaru_renderer_begin_mesh (vk_renderer_t *rend, uint32_t addr,
                                 bool is_static);
void hikaru_renderer_end_mesh (vk_renderer_t *rend, uint32_t addr);
void hikaru_renderer_push_vertices (vk_renderer_t *rend,
                                    hikaru_gpu_vertex_t *v,
                                    uint32_t push,
                                    unsigned num);

void		 hikaru_renderer_invalidate_texcache (vk_renderer_t *rend,
		                                      hikaru_gpu_texhead_t *th);
vk_surface_t	*hikaru_renderer_decode_texture (vk_renderer_t *rend,
		                                 hikaru_gpu_texhead_t *th);

#endif /* __HIKARU_GPU_PRIVATE_H__ */
