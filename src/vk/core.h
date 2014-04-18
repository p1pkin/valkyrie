/* 
 * Valkyrie
 * Copyright (C) 2011, 2012, Stefano Teso
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

#ifndef __VK_CORE_H__
#define __VK_CORE_H__

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <endian.h> /* See mesa p_config.h */

#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <GL/glew.h>
#include <SDL.h>

#include <jansson.h>

#include "vk/types.h"

#ifndef __BYTE_ORDER
#error "__BYTE_ORDER not defined"
#endif

/* Determine target endianess */
#if __BYTE_ORDER == __LITTLE_ENDIAN
# define VK_LITTLE_ENDIAN
#elif __BYTE_ORDER == __BIG_ENDIAN
# define VK_BIG_ENDIAN
#else
# error "unhandled __BYTE_ORDER value"
#endif

extern unsigned vk_verbosity;

typedef enum {
	VK_SWAP_NONE	= 0,
	VK_SWAP_BSWAP16	= (1 << 0),
	VK_SWAP_BSWAP32	= (1 << 1),

	VK_NUM_SWAP_TYPES
} vk_swap;

#define ALLOC(type_) \
	((type_ *) calloc (1, sizeof (type_)))

#define NUMELEM(arr_) \
	(sizeof (arr_) / sizeof ((arr_)[0]))

#define MIN2(a_,b_) \
	(((a_) < (b_)) ? (a_) : (b_))

#define MAX2(a_,b_) \
	(((a_) > (b_)) ? (a_) : (b_))

#define MIN3(a_,b_,c_) \
	(((a_) < (b_) && (a_) < (c_)) ? (a_) : \
	 ((b_) < (a_) && (b_) < (c_)) ? (b_) : (c_))

#define MAX3(a_,b_) \
	(((a_) > (b_) && (a_) > (c_)) ? (a_) : \
	 ((b_) > (a_) && (b_) > (c_)) ? (b_) : (c_))

#define MIN4(a,b,c,d) \
	(((a_) < (b_) && (a_) < (c_) && (a_) < (d_)) ? (a_) : \
	 ((b_) < (a_) && (b_) < (c_) && (b_) < (d_)) ? (b_) : \
	 ((c_) < (a_) && (c_) < (b_) && (c_) < (d_)) ? (b_) : (d_))

#define MAX4(a,b,c,d) \
	(((a_) > (b_) && (a_) > (c_) && (a_) > (d_)) ? (a_) : \
	 ((b_) > (a_) && (b_) > (c_) && (b_) > (d_)) ? (b_) : \
	 ((c_) > (a_) && (c_) > (b_) && (c_) > (d_)) ? (b_) : (d_))

static inline bool
is_pow2 (unsigned n)
{
	return !(n & (n - 1));
}

/* The following should automatically be detected as byte-swap operations by
 * the compiler and transformed into the corresponding CPU operation. No need
 * for hand-made assembly. */

static inline uint16_t
bswap16 (const uint16_t val)
{
	return ((val << 8) & 0xFF00) |
	       ((val >> 8) & 0x00FF);
}

static inline uint32_t
bswap32 (const uint32_t val)
{
	return ((val << 24) & 0xFF000000) |
	       ((val <<  8) & 0x00FF0000) |
	       ((val >>  8) & 0x0000FF00) |
	       ((val >> 24) & 0x000000FF);
}

static inline uint64_t
bswap64 (const uint64_t val)
{
	return ((val << 56) & 0xFF00000000000000ULL) |
	       ((val << 40) & 0x00FF000000000000ULL) |
	       ((val << 24) & 0x0000FF0000000000ULL) |
	       ((val <<  8) & 0x000000FF00000000ULL) |
	       ((val >>  8) & 0x00000000FF000000ULL) |
	       ((val >> 24) & 0x0000000000FF0000ULL) |
	       ((val >> 40) & 0x000000000000FF00ULL) |
	       ((val >> 56) & 0x00000000000000FFULL);
}

static inline uint16_t
#ifdef VK_LITTLE_ENDIAN
cpu_to_le16 (uint16_t a)
#else
cpu_to_be16 (uint16_t a)
#endif
{
	return a;
}

static inline uint32_t
#ifdef VK_LITTLE_ENDIAN
cpu_to_le32 (uint32_t a)
#else
cpu_to_be32 (uint32_t a)
#endif
{
	return a;
}

static inline uint64_t
#ifdef VK_LITTLE_ENDIAN
cpu_to_le64 (uint64_t a)
#else
cpu_to_be64 (uint64_t a)
#endif
{
	return a;
}

static inline uint16_t
#ifdef VK_LITTLE_ENDIAN
cpu_to_be16 (uint16_t a)
#else
cpu_to_le16 (uint16_t a)
#endif
{
	return bswap16 (a);
}

static inline uint32_t
#ifdef VK_LITTLE_ENDIAN
cpu_to_be32 (uint32_t a)
#else
cpu_to_le32 (uint32_t a)
#endif
{
	return bswap32 (a);
}

static inline uint64_t
#ifdef VK_LITTLE_ENDIAN
cpu_to_be64 (uint64_t a)
#else
cpu_to_le64 (uint64_t a)
#endif
{
	return bswap64 (a);
}

static inline uint64_t
cpu_to_be (unsigned size, uint64_t val) {
	switch (size) {
	case 2:
		val = cpu_to_be16 (val);
		break;
	case 4:
		val = cpu_to_be32 (val);
		break;
	case 8:
		val = cpu_to_be64 (val);
		break;
	}
	return val;
}

static inline uint64_t
cpu_to_le (unsigned size, uint64_t val) {
	switch (size) {
	case 2:
		val = cpu_to_le16 (val);
		break;
	case 4:
		val = cpu_to_le32 (val);
		break;
	case 8:
		val = cpu_to_le64 (val);
		break;
	}
	return val;
}

static inline int32_t
signext_n_32 (const uint32_t in, const unsigned sign_bit)
{
	const uint32_t bit  = 1 << sign_bit;
	const uint32_t mask = bit - 1;
	uint32_t out = in & mask;
	if (in & bit) {
		out |= ~1 & ~mask;
	}
	return (int32_t) out;
}

static inline int64_t
signext_n_64 (const uint64_t in, const unsigned sign_bit)
{
	const uint64_t bit  = 1ull << sign_bit;
	const uint64_t mask = bit - 1;
	uint64_t out = in & mask;
	if (in & bit) {
		out |= ~1 & ~mask;
	}
	return (int64_t) out;
}

/* If cond_ is zero, the array size will be negative and the compilation will
 * fail. Taken from Mesa */
#define VK_STATIC_ASSERT(cond_) \
	do { \
		typedef int static_assertion_failed[(!!(cond_))*2-1]; \
	} while (0);

/** Prints a formatted message to stdout */
#define VK_PRINT(fmt_, args_...) \
	do { \
		fprintf (stdout, fmt_"\n", ##args_); \
	} while (0)

/** Prints a formatted message to stdout in DEBUG builds, and only if verbosity
 * is non-zero */
#ifdef NDEBUG
#define VK_LOG(fmt_, args_...)
#else
#define VK_LOG(fmt_, args_...) \
	do { \
		if (vk_verbosity > 0) \
			fprintf (stdout, fmt_"\n", ##args_); \
	} while (0)
#endif

/** Prints a formatted message, prefixed by 'ERROR', to stderr */
#define VK_ERROR(fmt_, args_...) \
	do { \
		fprintf (stderr, "ERROR: "fmt_"\n", ##args_); \
	} while (0)

/** Prints a formatted message, prefixed by 'FATAL', to stderr, and aborts */
#define VK_ABORT(fmt_, args_...) \
	do { \
		fprintf (stderr, "FATAL: %s:%d %s(): "fmt_"\n", \
		         __FILE__, __LINE__, __FUNCTION__, ##args_); \
		abort (); \
	} while (0)

/** If the condition fails, prints a formatted message, prefixed by 'FATAL',
 * to stderr, and exits */
#ifdef NDEBUG
#define VK_ASSERT(_cond)
#else
#define VK_ASSERT(_cond) \
	do { \
		if (!(_cond)) { \
			VK_ABORT ("assertion failed, aborting"); \
		} \
	} while (0)
#endif

/* XXX the following is a big mess; clean it up please */

bool	is_valid_mat4x3f (mtx4x3f_t mtx);
bool	is_valid_mat4x4f (mtx4x4f_t mtx);

bool	vk_util_get_bool_option (const char *name, bool fallback);
int	vk_util_get_int_option (const char *name, int fallback);

#endif /* __VK_CORE_H__ */

