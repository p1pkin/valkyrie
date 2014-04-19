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

#include "mach/hikaru/hikaru-gpu.h"
#include "mach/hikaru/hikaru-gpu-private.h"
#include "mach/hikaru/hikaru-renderer.h"

void
get_texhead_coords (uint32_t *x, uint32_t *y, hikaru_texhead_t *tex)
{
	if (tex->slotx < 0x80 || tex->sloty < 0xC0) {
		/*
		 * This case is triggered by the following CP code, used by
		 * the BOOTROM:
		 *
		 * GPU CMD 480008B0: Texhead: Set Unknown [000100C1]
		 * GPU CMD 480008B4: Texhead: Set Format/Size [C08002C1]
		 * GPU CMD 480008B8: Texhead: Set Slot [0000C4C1]
		 * GPU CMD 480008BC: Commit Texhead [000010C4] n=0 e=1
		 * GPU CMD 480008C0: Recall Texhead [000010C3] () n=0 e=1
		 *
		 * Note how the params to 2C1 and 4C1 are swapped.
		 */
		VK_ERROR ("GPU: invalid slot %X,%X", tex->slotx, tex->sloty);
		*x = 0;
		*y = 0;
	} else {
		*x = (tex->slotx - 0x80) * 16;
		*y = (tex->sloty - 0xC0) * 16;
	}
}

/* XXX tentative, mirrors OpenGL's ordering. */
static const char depth_func_name[8][3] = {
	"NV", "LT", "EQ", "LE",
	"GT", "NE", "GE", "AW"
};

static const char *none = "NONE";

const char *
get_viewport_str (hikaru_viewport_t *vp)
{
	static char out[512];

	if (!vp)
		return none;

	sprintf (out, "clip=(%6.3f %6.3f %6.3f %6.3f %6.3f %6.3f) offs=(%6.3f %6.3f) depth=(%s %6.3f %6.3f)",
	         vp->clip.l, vp->clip.r,
	         vp->clip.b, vp->clip.t,
	         vp->clip.f, vp->clip.n,
	         vp->offset.x, vp->offset.y,
	         depth_func_name [vp->depth.func],
	         vp->depth.min, vp->depth.max);

	return (const char *) out;
}

const char *
get_modelview_str (hikaru_modelview_t *mv)
{
	static char out[512];

	if (!mv)
		return none;

	sprintf (out,
	         "|%f %f %f %f| |%f %f %f %f| |%f %f %f %f| |%f %f %f %f|",
	         mv->mtx[0][0], mv->mtx[0][1], mv->mtx[0][2], mv->mtx[0][3],
	         mv->mtx[1][0], mv->mtx[1][1], mv->mtx[1][2], mv->mtx[1][3],
	         mv->mtx[2][0], mv->mtx[2][1], mv->mtx[2][2], mv->mtx[2][3],
	         mv->mtx[3][0], mv->mtx[3][1], mv->mtx[3][2], mv->mtx[3][3]);

	return (const char *) out;
}

const char *
get_material_str (hikaru_material_t *mat)
{
	static char out[512];

	if (!mat)
		return none;

	sprintf (out, "#%02X%02X%02X,%02X #%02X%02X%02X #%02X%02X%02X,%02X #%04X,%04X,%04X 081=%08X 881=%08X A81=%08X C81=%08X",
	         mat->diffuse[0], mat->diffuse[1], mat->diffuse[2], mat->diffuse[3],
	         mat->ambient[0], mat->ambient[1], mat->ambient[2],
	         mat->specular[0], mat->specular[1], mat->specular[2], mat->specular[3],
	         mat->unknown[0], mat->unknown[1], mat->unknown[2],
	         mat->_081, mat->_881, mat->_A81, mat->_C81);

	return (const char *) out;
}

const char *
get_texhead_str (hikaru_texhead_t *th)
{
	static const char *name[8] = {
		"RGBA5551",
		"RGBA4444",
		"RGBA1111",
		"???3??? ",
		"ALPHA8  ",
		"???5??? ",
		"???6??? ",
		"???7??? "
	};
	static char out[512];
	uint32_t basex, basey;

	if (!th)
		return none;

	get_texhead_coords (&basex, &basey, th);

	sprintf (out, "[bank=%X slot=(%2X,%2X) pos=(%3X,%3X) -> offs=%08X] [size=%3ux%3u format=%s] 0C1=%08X 2C1=%08X",
	         th->bank, th->slotx, th->sloty,
	         basex, basey, basey*4096 + basex*2,
	         16 << th->logw, 16 << th->logh,
	         name[th->format], th->_0C1, th->_2C1);

	return (const char *) out;
}

const char *
get_light_str (hikaru_light_t *lit)
{
	static char out[512];

	if (!lit)
		return none;

	sprintf (out, "%u (%+10.3f %+10.3f) dir=%u (%+10.3f %+10.3f %+10.3f) pos=%u (%+10.3f %+10.3f %+10.3f) [%u %03X %03X %03X] [%u %02X %02X %02X]",
	         lit->att_type, lit->attenuation[0], lit->attenuation[1],
	         lit->has_direction, lit->direction[0], lit->direction[1], lit->direction[2],
	         lit->has_position, lit->position[0], lit->position[1], lit->position[2],
	         lit->_051_index, lit->diffuse[0], lit->diffuse[0], lit->diffuse[0],
	         lit->has_specular, lit->specular[0], lit->specular[0], lit->specular[0]);

	return (const char *) out;
};

const char *
get_lightset_str (hikaru_lightset_t *ls)
{
	static char out[512];
	char *tmp = &out[0];
	unsigned i;

	for (i = 0; i < 4; i++) {
		if (ls->mask & (1 << i))
			continue;
		tmp += sprintf (tmp, "%s\n", get_light_str (&ls->lights[i]));
	}

	return (const char *) out;
}

const char *
get_vertex_str (hikaru_vertex_t *v)
{
	static char out[512];
	char *tmp = &out[0];

	tmp += sprintf (tmp, "[T=%X t=%u p=%u w=%u] ",
	                v->info.tricap, v->info.tpivot,
	                v->info.ppivot, v->info.winding);

	tmp += sprintf (tmp, "(X: %5.3f %5.3f %5.3f) ",
	                v->body.position[0], v->body.position[1], v->body.position[2]);
	tmp += sprintf (tmp, "(N: %5.3f %5.3f %5.3f) ",
	                v->body.normal[0], v->body.normal[1], v->body.normal[2]);
	tmp += sprintf (tmp, "(Cd: %02X %02X %02X) ",
	                v->body.diffuse[0], v->body.diffuse[1], v->body.diffuse[2]);
	tmp += sprintf (tmp, "(Ca: %02X %02X %02X) ",
	                v->body.ambient[0], v->body.ambient[1], v->body.ambient[2]);
	tmp += sprintf (tmp, "(Cs: %02X %02X %02X | %02X) ",
	                v->body.specular[0], v->body.specular[1], v->body.specular[2], v->body.specular[3]);
	tmp += sprintf (tmp, "(Cu: %04X %04X %04X) ",
	                v->body.unknown[0], v->body.unknown[1], v->body.unknown[2]);
	tmp += sprintf (tmp, "(T: %5.3f %5.3f) ",
	                v->body.texcoords[0], v->body.texcoords[1]);

	return out;
}

const char *
get_layer_str (hikaru_layer_t *layer)
{
	static char out[256];
	sprintf (out, "ena=%u (%u,%u) (%u,%u) fmt=%u", layer->enabled,
	         layer->x0, layer->y0, layer->x1, layer->y1, layer->format);
	return (const char *) out;
}
