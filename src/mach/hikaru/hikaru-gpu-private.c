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
slot_to_coords (uint32_t *basex, uint32_t *basey, uint32_t slotx, uint32_t sloty)
{
	if (slotx < 0x80 || sloty < 0xC0) {
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
		VK_ERROR ("GPU: invalid slot %X,%X", slotx, sloty);
		*basex = 0;
		*basey = 0;
	} else {
		*basex = (slotx - 0x80) * 16;
		*basey = (sloty - 0xC0) * 16;
	}
}

/* XXX tentative, mirrors OpenGL's ordering. */
static const char depth_func_name[8][3] = {
	"NV", "LT", "EQ", "LE",
	"GT", "NE", "GE", "AW"
};

const char *
get_gpu_viewport_str (hikaru_gpu_viewport_t *vp)
{
	static char out[512];

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
get_gpu_modelview_str (hikaru_gpu_modelview_t *modelview)
{
	static char out[512];

	sprintf (out,
	         "|%f %f %f %f| |%f %f %f %f| |%f %f %f %f| |%f %f %f %f|",
	         modelview->mtx[0][0], modelview->mtx[0][1], modelview->mtx[0][2], modelview->mtx[0][3],
	         modelview->mtx[1][0], modelview->mtx[1][1], modelview->mtx[1][2], modelview->mtx[1][3],
	         modelview->mtx[2][0], modelview->mtx[2][1], modelview->mtx[2][2], modelview->mtx[2][3],
	         modelview->mtx[3][0], modelview->mtx[3][1], modelview->mtx[3][2], modelview->mtx[3][3]);

	return (const char *) out;
}

const char *
get_gpu_material_str (hikaru_gpu_material_t *material)
{
	static char out[512];

	sprintf (out, "Col0=#%02X%02X%02X Col1=#%02X%02X%02X Shin=%u,#%02X%02X%02X Mat=#%04X,%04X,%04X ShadingMode=%u ZBlend=%u Tex=%u Alpha=%u High=%u BlendMode=%u",
	         material->color[0][0], material->color[0][1], material->color[0][2],
	         material->color[1][0], material->color[1][1], material->color[1][2],
	         material->shininess,
	         material->specularity[0],
	         material->specularity[1],
	         material->specularity[2],
	         material->material_color[0],
	         material->material_color[1],
	         material->material_color[2],
	         material->shading_mode, material->depth_blend,
	         material->has_texture, material->has_alpha,
	         material->has_highlight, material->blending_mode);

	return (const char *) out;
}

const char *
get_gpu_texhead_str (hikaru_gpu_texhead_t *texhead)
{
	static const char *name[8] = {
		"RGBA5551",
		"RGBA4444",
		"RGBA1111",
		"???3???",
		"ALPHA8",
		"???5???",
		"???6???",
		"???7???"
	};
	static char out[512];
	uint32_t basex, basey;

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty);

	sprintf (out, "slot=(%X,%X) pos=(%X,%X) offs=%08X %ux%u %s mode=%X value=%X wrap=[%u %u|%u %u] unk=%X bank=%X",
	         texhead->slotx, texhead->sloty, basex, basey,
	         basey*4096 + basex*2,
	         texhead->width, texhead->height,
	         name[texhead->format],
	         texhead->_0C1_mode, texhead->_0C1_value,
	         texhead->wrap_u, texhead->repeat_u,
	         texhead->wrap_v, texhead->repeat_v,
	         texhead->_2C1_unk, texhead->bank);

	return (const char *) out;
}

const char *
get_gpu_light_str (hikaru_gpu_light_t *light)
{
	static char out[512];

	sprintf (out, "(%5.3f %5.3f %u) (%7.3f %7.3f %7.3f) (%7.3f %7.3f %7.3f)",
	         light->emission_p, light->emission_q, light->emission_type,
	         light->vec9[0], light->vec9[1], light->vec9[2],
	         light->vecB[0], light->vecB[1], light->vecB[2]);

	return (const char *) out;
};

const char *
get_gpu_vertex_str (hikaru_gpu_vertex_t *v)
{
	static char out[512];
	char *tmp = &out[0];

	tmp += sprintf (tmp, "[T=%X t=%u p=%u w=%u] ",
	                v->info.bit.tricap, v->info.bit.tpivot,
	                v->info.bit.ppivot, v->info.bit.winding);

	tmp += sprintf (tmp, "(X: %5.3f %5.3f %5.3f) ",
	                v->pos[0], v->pos[1], v->pos[2]);
	tmp += sprintf (tmp, "(C: %5.3f %5.3f %5.3f %5.3f) ",
	                v->col[0], v->col[1], v->col[2], v->col[3]);
	tmp += sprintf (tmp, "(N: %5.3f %5.3f %5.3f) ",
	                v->nrm[0], v->nrm[1], v->nrm[2]);
	tmp += sprintf (tmp, "(T: %5.3f %5.3f) ",
	                v->txc[0], v->txc[1]);

	return out;
}

const char *
get_gpu_layer_str (hikaru_gpu_layer_t *layer)
{
	static char out[256];
	sprintf (out, "ena=%u (%u,%u) (%u,%u) fmt=%u", layer->enabled,
	         layer->x0, layer->y0, layer->x1, layer->y1, layer->format);
	return (const char *) out;
}
