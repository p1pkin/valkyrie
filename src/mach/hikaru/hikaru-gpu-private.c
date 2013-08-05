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

const char *
get_gpu_viewport_str (hikaru_gpu_viewport_t *viewport)
{
	static char out[512];

	sprintf (out, "(%8.3f %8.3f %8.3f) (%u,%u) (%u,%u) (%u,%u) (%u %5.3f %5.3f) (%u %5.3f %5.3f)",
	         viewport->persp_x, viewport->persp_y, viewport->persp_znear,
	         viewport->center[0], viewport->center[1],
	         viewport->extents_x[0], viewport->extents_x[1],
	         viewport->extents_y[0], viewport->extents_y[1],
	         viewport->depth_func, viewport->depth_near, viewport->depth_far,
	         viewport->depthq_type, viewport->depthq_density, viewport->depthq_bias);

	return (const char *) out;
}

const char *
get_gpu_material_str (hikaru_gpu_material_t *material)
{
	static char out[512];

	sprintf (out, "Col0=#%02X%02X%02X Col1=#%02X%02X%02X Shin=%u,#%02X%02X%02X Mat=#%04X,%04X,%04X ShadingMode=%u ZBlend=%u Tex=%u Alpha=%u High=%u BlendMode=%u",
	         material->color[0][0], material->color[0][1], material->color[0][2],
	         material->color[1][0], material->color[1][1], material->color[1][2],
	         material->specularity,
	         material->shininess[0],
	         material->shininess[1],
	         material->shininess[2],
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

	sprintf (out, "slot=(%X,%X) pos=(%X,%X) offs=%08X %ux%u %s ni=%X by=%X u4=%X u8=%X bank=%X",
	         texhead->slotx, texhead->sloty, basex, basey,
	         basey*4096 + basex*2,
	         texhead->width, texhead->height,
	         name[texhead->format],
	         texhead->_0C1_nibble, texhead->_0C1_byte,
	         texhead->_2C1_unk4, texhead->_2C1_unk8,
	         texhead->bank);

	return (const char *) out;
}

const char *
get_gpu_light_str (hikaru_gpu_light_t *light)
{
	static char out[512];

	sprintf (out, "(%5.3f %5.3f %u) (%7.3f 7.3%f 7.3%f) (%7.3f %7.3f %7.3f)",
	         light->emission_p, light->emission_q, light->emission_type,
	         light->position[0], light->position[1], light->position[2],
	         light->direction[0], light->direction[1], light->direction[2]);

	return (const char *) out;
};

const char *
get_gpu_layer_str (hikaru_gpu_layer_t *layer)
{
	static char out[256];
	sprintf (out, "(%u,%u) (%u,%u) fmt=%u",
	         layer->x0, layer->y0, layer->x1, layer->y1, layer->format);
	return (const char *) out;
}
