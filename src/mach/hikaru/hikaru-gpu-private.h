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

#ifndef __HIKARU_GPU_PRIVATE_H__
#define __HIKARU_GPU_PRIVATE_H__

#include <vk/device.h>

/* All of these are tentative */
#define NUM_VIEWPORTS	8
#define NUM_MATERIALS	8192
#define NUM_TEXHEADS	8192
#define NUM_LIGHTS	1024
#define NUM_MATRICES	4

#define VIEWPORT_MASK	(NUM_VIEWPORTS - 1)
#define MATERIAL_MASK	(NUM_MATERIALS - 1)
#define TEXHEAD_MASK	(NUM_TEXHEADS - 1)
#define LIGHT_MASK	(NUM_LIGHTS - 1)
#define MATRIX_MASK	(NUM_MATRICES - 1)

typedef struct {
	/* 021 */
	float persp_x;
	float persp_y;
	float persp_unk;
	/* 221 */
	vec2s_t center;
	vec2s_t extents_x;
	vec2s_t extents_y;
	/* 421 */
	float depth_near;
	float depth_far;
	unsigned depth_func;
	/* 621 */
	uint32_t depthq_type;
	uint32_t depthq_enabled;
	uint32_t depthq_unk;
	vec4b_t	depthq_mask;
	float depthq_density;
	float depthq_bias;
	/* 881 */
	vec3s_t ambient_color;
	/* 991 */
	vec4b_t clear_color;
	/* Util */
	uint32_t used : 1;
} hikaru_gpu_viewport_t;

typedef struct {
	/* 091, 291 */
	vec3b_t color[2];
	/* 491 */
	uint8_t specularity;
	vec3b_t shininess;
	/* 691 */
	vec3s_t material_color;
	/* 081 */
	/* XXX unknown */
	/* 881 */
	unsigned mode		: 2;
	unsigned depth_blend	: 1;
	unsigned has_texture	: 1;
	unsigned has_alpha	: 1;
	unsigned has_highlight	: 1;
	/* A81 */
	unsigned bmode		: 2;
	/* C81 */
	/* XXX unknown */
	/* Util */
	uint32_t used : 1;
} hikaru_gpu_material_t;

enum {
	FORMAT_RGBA5551 = 0,
	FORMAT_RGBA4444 = 1,
	FORMAT_RGBA1111 = 2, /* Or PAL16... */
	FORMAT_ALPHA8 = 4,
};

typedef struct {
	/* 0C1 */
	uint32_t _0C1_nibble;
	uint32_t _0C1_byte;
	/* 2C1 */
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t _2C1_unk4;
	uint32_t _2C1_unk8;
	/* 4C1 */
	uint32_t slotx;
	uint32_t sloty;
	uint32_t _4C1_unk;
	uint32_t used : 1;
} hikaru_gpu_texhead_t;

typedef struct {
	uint32_t addr;
	uint32_t size;
	uint32_t slotx;
	uint32_t sloty;
	uint32_t width;
	uint32_t height;
	uint32_t format : 3;
	uint32_t bank : 1;
	uint32_t has_mipmap : 1;
} hikaru_gpu_texture_t;

typedef struct {
	/* 261 */
	unsigned type;
	float param_p;
	float param_q;
	/* 961 */
	vec3f_t position;
	/* B61 */
	vec3f_t direction;
} hikaru_gpu_light_t;

typedef struct {
	unsigned num_lights;
	hikaru_gpu_light_t lights[8];
} hikaru_gpu_lightset_t;

typedef struct {
	vk_device_t base;

	vk_buffer_t *cmdram;
	vk_buffer_t *texram;
	vk_buffer_t *unkram[2];

	vk_renderer_t *renderer;

	uint8_t regs_15[0x100];
	uint8_t regs_18[0x100];
	uint8_t regs_1A[0x104];
	uint8_t regs_1A_unit[2][0x40];
	uint8_t regs_1A_fifo[0x10];

	/* CS Execution State */

	struct {
		uint32_t pc, sp[2];
		bool is_running;
		unsigned frame_type;
		int cycles;
	} cs;

	/* Rendering State */

	struct {
		hikaru_gpu_viewport_t table[NUM_VIEWPORTS];
		hikaru_gpu_viewport_t scratch;
		int offset;
	} viewports;

	struct {
		hikaru_gpu_material_t table[NUM_MATERIALS];
		hikaru_gpu_material_t scratch;
		int offset;
	} materials;

	struct {
		hikaru_gpu_texhead_t table[NUM_TEXHEADS];
		hikaru_gpu_texhead_t scratch;
		int offset;
	} texheads;

	struct {
		hikaru_gpu_light_t table[NUM_LIGHTS];
		hikaru_gpu_light_t scratch;
		int offset;
	} lights;

	mtx4x3f_t mtx[NUM_MATRICES];

} hikaru_gpu_t;

char *get_gpu_viewport_str (hikaru_gpu_viewport_t *);
char *get_gpu_material_str (hikaru_gpu_material_t *);
char *get_gpu_texhead_str (hikaru_gpu_texhead_t *);
char *get_gpu_texture_str (hikaru_gpu_texture_t *);

#endif /* __HIKARU_GPU_PRIVATE_H__ */
