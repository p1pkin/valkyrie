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

#ifndef __VK_TYPES_H__
#define __VK_TYPES_H__

#include <stdint.h>

#define HZ	1
#define KHZ	1000
#define MHZ	(1000*1000)

#define KB	(1024)		/* in bytes */
#define MB	(1024*1024)	/* in bytes */

#define NSEC	1		/* in nanoseconds */
#define USEC	1000		/* in nanoseconds */
#define MSEC	(1000*1000)	/* in nanoseconds */

/* Composite Types */

typedef union {
	struct {
		uint32_t lo;
		uint32_t hi;
	} field;
	uint64_t full;
} pair32u_t;

/* Alias Types */

typedef union {
	uint32_t u;
	float f;
} alias32uf_t;

typedef union {
	uint64_t u;
	double f;
} alias64uf_t;

/* Vectors */

typedef uint8_t vec3b_t[3];
typedef uint8_t vec4b_t[4];

typedef uint16_t vec2s_t[2];
typedef uint16_t vec3s_t[3];

typedef float vec2f_t[2];
typedef float vec3f_t[3];

/* Matrices */

typedef float mtx3x3f_t[3][3];
typedef float mtx4x3f_t[4][3];
typedef float mtx4x4f_t[4][4];

#endif /* __VK_TYPES_H__ */
