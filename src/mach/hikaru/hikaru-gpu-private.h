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

/* All of these are tentative */
#define NUM_VIEWPORTS	32
#define NUM_MATERIALS	256
#define NUM_TEXHEADS	256
#define NUM_MATRICES	16
#define NUM_LIGHTS	16

#define VIEWPORT_MASK	(NUM_VIEWPORTS - 1)
#define MATERIAL_MASK	(NUM_MATERIALS - 1)
#define TEXHEAD_MASK	(NUM_TEXHEADS - 1)
#define MATRIX_MASK	(NUM_MATRICES - 1)
#define LIGHT_MASK	(NUM_LIGHTS - 1)

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
	unsigned unk_func;
	float unk_n;
	float unk_b;
	/* 621 */
	uint32_t depth_type;
	uint32_t depth_enabled;
	uint32_t depth_unk;
	vec4b_t	depth_mask;
	float depth_density;
	float depth_bias;
	/* 881 */
	vec3s_t ambient_color;
	/* 991 */
	vec4b_t clear_color;
} hikaru_gpu_viewport_t;

typedef struct {
	/* 091, 291 */
	vec3b_t color[2];
	/* 491 */
	vec4b_t shininess;
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
} hikaru_gpu_material_t;

typedef struct {
	/* 0C1 */
	uint8_t _0C1_n : 4;
	uint8_t _0C1_m : 4;
	/* 2C1 */
	uint8_t _2C1_a;
	uint8_t _2C1_b;
	uint8_t _2C1_u : 4;
	/* 4C1 */
	uint8_t _4C1_n;
	uint8_t _4C1_m;
	uint8_t _4C1_p : 4;
} hikaru_gpu_texhead_t;

typedef struct {
	/* TODO */
	unsigned dummy;
} hikaru_gpu_light_t;

#endif /* __HIKARU_GPU_PRIVATE_H__ */
