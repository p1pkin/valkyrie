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

#include <math.h>

#include "mach/hikaru/hikaru-renderer.h"
#include "vk/surface.h"

/*
 * Here's the basics. The three entry points to the renderer are:
 *
 * - hikaru_renderer_{begin,end}_frame (), called by hikaru.c when the
 *   appropriate number of core iterations have been done.
 *
 * - hikaru_renderer_draw_layer (), called by hikaru-gpu.c just on vblank-out,
 *   that is, just before end_frame () is called.
 *
 * - hikaru_renderer_{set_X, add_vertex_X, end_mesh} (), called by
 *   hikaru-gpu-insns.c when the corresponding GPU instructions are
 *   interpreted; which is to say that these calls occur at any time between
 *   begin_frame () and draw_layer ().
 *
 * XXX The order in which things are done is *not* correct: the layer
 * priority wrt to the 3D meshes have not been figured out yet. This is
 * partly due to the fact that I *don't know* when the CP is supposed to
 * execute the command stream (at vblank-in? when 15000058 is written to?
 * I have found no strong clue about which is true...)
 *
 * A mesh set through the add_vertex_X () functions is rendered when
 * end_mesh () is called, using draw_current_mesh ().
 *
 * The set_X () functions merely store the corresponding state (i.e., the
 * current viewport, modelview matrix, material, texhead, and lightset) into
 * members of hikaru_renderer_t.
 *
 * This state is not uploaded to GL until a call to draw_current_mesh ()
 * occurs. The actual uploading is done using the upload_current_X ()
 * functions. We also make sure that the same state is not re-uploaded to
 * GL, to avoid useless computations.
 *
 * XXX of course, the whole thing could be improved quite a bit by any
 * developer even moderately knowledgeable of GL; unfortunately I don't belong
 * to that circle, by far :-)
 */

/* XXX dump textures on exit if requested. */
/* XXX ditch the fixed pipeline; use shaders instead. */
/* XXX ditch immediate mode; use VBOs instead. */

/* This is *not* the native hikaru vertex data layout; this is how *we*
 * arrange stuff for now (and possibly for future VBO use.) */
typedef struct {
	vec3f_t pos;		/* 12 bytes */
	vec3f_t normal;		/* 12 bytes */
	vec2f_t texcoords;	/* 8 bytes */
} hikaru_vertex_t;

typedef enum {
	MESH_TYPE_TRI_LIST,	/* used by commands 1A8 et co. */
	MESH_TYPE_TRI_STRIP	/* used by commands 158 et co. */
} hikaru_mesh_type_t;

typedef struct {
	hikaru_mesh_type_t type;
	hikaru_vertex_t *vertices;
	unsigned index, tindex, max;
} hikaru_mesh_t;

typedef struct {
	mtx4x4f_t mtx;
	uint32_t set		: 1;
	uint32_t uploaded	: 1;
} hikaru_gpu_modelview_t;

typedef struct {
	vk_renderer_t base;

	vk_buffer_t *fb;
	vk_buffer_t *texram[2];

	/* Current rendering state */
	struct {
		hikaru_gpu_viewport_t	viewport;
		hikaru_gpu_modelview_t	modelview;
		hikaru_gpu_texhead_t	texhead;
		hikaru_gpu_material_t	material;
		hikaru_gpu_lightset_t	lightset;
		hikaru_mesh_t		*mesh;
		vk_surface_t		*texture;
	} current;

	/* Mesh data */
	struct {
		hikaru_mesh_t *array;
		unsigned index, max;
	} meshes;

	/* Texture data */
	struct {
		vk_surface_t *fb, *texram, *debug;
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

#define LOG(fmt_, args_...) \
	do { \
		if (hr->options.log) \
			fprintf (stdout, "HR: " fmt_"\n", ##args_); \
	} while (0);

/* Textures */

static uint32_t
rgba1111_to_rgba4444 (uint8_t pixel)
{
	static const uint32_t table[16] = {
		0x0000, 0xF000, 0x0F00, 0xFF00,
		0x00F0, 0xF0F0, 0x0FF0, 0xFFF0,
		0x000F, 0xF00F, 0x0F0F, 0xFF0F,
		0x00FF, 0xF0FF, 0x0FFF, 0xFFFF,
	};

	return table[pixel & 15];
}

static vk_surface_t *
decode_texhead_rgba1111 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->texram[texhead->bank];
	uint32_t basex, basey, x, y;
	vk_surface_t *surface;

	surface = vk_surface_new (texhead->width, texhead->height*2, VK_SURFACE_FORMAT_RGBA4444);
	if (!surface)
		return NULL;

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty); 

	for (y = 0; y < texhead->height; y ++) {
		for (x = 0; x < texhead->width; x += 4) {
			uint32_t offs = (basey + y) * 4096 + (basex + x);
			uint32_t texels = vk_buffer_get (texram, 4, offs);
			vk_surface_put16 (surface, x + 0, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >> 28));
			vk_surface_put16 (surface, x + 1, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >> 24));
			vk_surface_put16 (surface, x + 0, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >> 20));
			vk_surface_put16 (surface, x + 1, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >> 16));
			vk_surface_put16 (surface, x + 2, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >> 12));
			vk_surface_put16 (surface, x + 3, y*2 + 0,
			                  rgba1111_to_rgba4444 (texels >>  8));
			vk_surface_put16 (surface, x + 2, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >>  4));
			vk_surface_put16 (surface, x + 3, y*2 + 1,
			                  rgba1111_to_rgba4444 (texels >>  0));
		}
	}
	return surface;
}

static vk_surface_t *
decode_texhead_rgba_16 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	vk_buffer_t *texram = hr->texram[texhead->bank];
	uint32_t basex, basey, x, y;
	vk_surface_format_t format;
	vk_surface_t *surface;

	if (texhead->format == HIKARU_FORMAT_RGBA5551)
		format = VK_SURFACE_FORMAT_RGBA5551;
	else
		format = VK_SURFACE_FORMAT_RGBA4444;

	surface = vk_surface_new (texhead->width, texhead->height, format);
	if (!surface)
		return NULL;

	slot_to_coords (&basex, &basey, texhead->slotx, texhead->sloty); 

	for (y = 0; y < texhead->height; y++) {
		uint32_t base = (basey + y) * 4096 + basex * 2;
		for (x = 0; x < texhead->width; x++) {
			uint32_t offs  = base + x * 2;
			uint16_t texel = vk_buffer_get (texram, 2, offs);
			vk_surface_put16 (surface, x, y, texel);
		}
	}
	return surface;
}

static vk_surface_t *
decode_texhead_a8 (hikaru_renderer_t *hr, hikaru_gpu_texhead_t *texhead)
{
	/* TODO */
	return NULL;
}

static void
free_textures (hikaru_renderer_t *hr)
{
	(void) hr;
}

/* 3D Rendering */

static bool
is_viewport_valid (hikaru_gpu_viewport_t *viewport)
{
	if (!isfinite (viewport->persp_x) ||
	    !isfinite (viewport->persp_y) ||
	    !isfinite (viewport->persp_znear)) {
		VK_ERROR ("viewport persp params not finite: %f, %f, %f",
		          viewport->persp_x, viewport->persp_y,
		          viewport->persp_znear);
		return false;
	}
	if (!isfinite (viewport->depth_near) ||
	    !isfinite (viewport->depth_far)) {
		VK_ERROR ("viewport depth params not finite: %f, %f",
		          viewport->depth_near, viewport->depth_far);
		return false;
	}
	if (!isfinite (viewport->depthq_density) ||
	    !isfinite (viewport->depthq_bias)) {
		VK_ERROR ("viewport depth queue params not finite: %f, %f",
		          viewport->depthq_density, viewport->depthq_bias);
		return false;
	}
	if (viewport->center[0] < viewport->extents_x[0] ||
	    viewport->center[0] > viewport->extents_x[1] ||
	    viewport->extents_x[1] > 640) {
		VK_ERROR ("viewport extents X invalid: %u < %u < %u",
		          viewport->extents_x[0], viewport->center[0],
		          viewport->extents_x[1]);
		return false;
	}
	if (viewport->center[1] < viewport->extents_y[0] ||
	    viewport->center[1] > viewport->extents_y[1] ||
	    viewport->extents_y[1] > 512) {
		VK_ERROR ("viewport extents Y invalid: %u < %u < %u",
		          viewport->extents_y[0], viewport->center[0],
		          viewport->extents_y[1]);
		return false;
	}
	if (viewport->persp_znear != viewport->depth_near) {
		VK_ERROR ("viewport znear 021-421 mismatch: %f != %f",
		          viewport->persp_znear, viewport->depth_near);
		/* no need to return false here, this is not critical */
	}
	return true;
}

void
hikaru_renderer_set_viewport (vk_renderer_t *renderer,
                              hikaru_gpu_viewport_t *viewport)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->options.disable_3d)
		return;

	LOG ("set viewport %s", get_gpu_viewport_str (viewport));

	VK_ASSERT (viewport->set);

	/* XXX clear the viewport now or in update()? */

	/* Store the current viewport state */
	if (is_viewport_valid (viewport)) {
		hr->current.viewport = *viewport;
		hr->current.viewport.uploaded = 0;
	}
}

void
hikaru_renderer_set_modelview_vector (vk_renderer_t *renderer,
                                      unsigned n, unsigned m,
                                      vec3f_t vector)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->options.disable_3d)
		return;

	LOG ("set modelview vector %u (%u): %7.3f %7.3f %7.3f",
	     n, m, vector[0], vector[1], vector[2]);

	/* XXX no idea what modelview matrices with m index > 0 are for */
	if (m != 0)
		return;

	if (isfinite (vector[0]) &&
	    isfinite (vector[1]) &&
	    isfinite (vector[2])) {
		/* It's a row vector; translate to column here */
		/* XXX use a macro to access the right element here */
		hr->current.modelview.mtx[0][n] = vector[0];
		hr->current.modelview.mtx[1][n] = vector[1];
		hr->current.modelview.mtx[2][n] = vector[2];
		hr->current.modelview.mtx[3][n] = (n == 3) ? 1.0f : 0.0f;
		hr->current.modelview.uploaded = 0;
	}
}

void
hikaru_renderer_set_material (vk_renderer_t *renderer,
                              hikaru_gpu_material_t *material)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->options.disable_3d)
		return;

	LOG ("set material %s", get_gpu_material_str (material));

	VK_ASSERT (material->set);

	/* Store the current material state */
	hr->current.material = *material;
	hr->current.material.uploaded = 0;
}

void
hikaru_renderer_set_texhead (vk_renderer_t *renderer,
                             hikaru_gpu_texhead_t *texhead)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	if (hr->options.disable_3d)
		return;

	LOG ("set texhead %s", get_gpu_texhead_str (texhead));

	VK_ASSERT (texhead->set);

	/* Store the current texhead state */
	hr->current.texhead = *texhead;
	hr->current.texhead.uploaded = 0;
}

void
hikaru_renderer_set_lightset (vk_renderer_t *renderer,
                              hikaru_gpu_lightset_t *lightset,
                              uint32_t enabled_mask)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	unsigned i;

	if (hr->options.disable_3d)
		return;

	for (i = 0; i < 4; i++) {
		if (enabled_mask & (1 << i))
			LOG ("set lightset, light %u: %s",
				i, get_gpu_light_str (lightset->lights[i]));
	}

	VK_ASSERT (lightset->set);

	/* Store the current lightset state */
	hr->current.lightset = *lightset;
	hr->current.lightset.uploaded = 0;
}

static void
upload_current_viewport (hikaru_renderer_t *hr)
{
	hikaru_gpu_viewport_t *vp = &hr->current.viewport;
	if (vp && !vp->uploaded) {
		vp->uploaded = 1;

		/* Setup the projection matrix */
		/* XXX compute the actual projection */
		glMatrixMode (GL_PROJECTION);
		glLoadIdentity ();
		glOrtho ((GLfloat) vp->extents_x[0],	/* left */
		         (GLfloat) vp->extents_x[1],	/* right */
		         -(GLfloat) vp->extents_y[1],	/* bottom */
		         (GLfloat) vp->extents_y[0],	/* top */
		         -1.0f, 1.0f);			/* near, far */
	
		/* Setup the vp scissor */
		glScissor (vp->extents_x[0], /* lower left */
		           vp->extents_y[0],
		           vp->extents_x[1] - vp->extents_x[0],
		           vp->extents_y[1] - vp->extents_y[0]);
		glEnable (GL_SCISSOR_TEST);
	
		/* Clear the vp */
		glClearColor (vp->clear_color[0] / 255.0f,
		              vp->clear_color[1] / 255.0f,
		              vp->clear_color[2] / 255.0f,
		              vp->clear_color[3] / 255.0f);
		glClear (GL_COLOR_BUFFER_BIT);
	}
}

static void
upload_current_modelview (hikaru_renderer_t *hr)
{
	if (!hr->current.modelview.uploaded) {
		hr->current.modelview.uploaded = 1;

		glMatrixMode (GL_MODELVIEW);
		glLoadMatrixf ((GLfloat *) &hr->current.modelview.mtx[0][0]);
	}
}

static void
upload_current_material (hikaru_renderer_t *hr)
{
	hikaru_gpu_material_t *mat = &hr->current.material;
	if (mat && !mat->uploaded) {
		mat->uploaded = 1;

		/* XXX we only upload color 1 here, as it is used by the
		 * BOOTROM menu. We should upload a lot more state to
		 * uniforms, once the shader renderer lands. */

		glColor3f (mat->color[1][0] / 255.0f,
		           mat->color[1][1] / 255.0f,
		           mat->color[1][2] / 255.0f);

		if (mat->has_texture)
			glEnable (GL_TEXTURE_2D);
		else
			glDisable (GL_TEXTURE_2D);
	}
}

static void
upload_current_texhead (hikaru_renderer_t *hr)
{
	hikaru_gpu_texhead_t *tex = &hr->current.texhead;
	if (tex && !tex->uploaded) {
		vk_surface_t *surface = NULL;

		tex->uploaded = 1;

		/* Delete the old texture */
		if (hr->current.texture != hr->textures.debug)
			vk_surface_destroy (&hr->current.texture);

		/* XXX implement texhead caching */

		/* Decode the texhead data into a vk_surface */
		switch (tex->format) {
		case HIKARU_FORMAT_RGBA5551:
		case HIKARU_FORMAT_RGBA4444:
			surface = decode_texhead_rgba_16 (hr, tex);
			break;
		case HIKARU_FORMAT_RGBA1111:
			surface = decode_texhead_rgba1111 (hr, tex);
			break;
		case HIKARU_FORMAT_ALPHA8:
			surface = decode_texhead_a8 (hr, tex);
			break;
		default:
			VK_ASSERT (0);
			break;
		}

		/* If no surface can be generated for the given texhead, bind
		 * the debug texture. */
		if (!surface || hr->options.force_debug_texture)
			vk_surface_bind (hr->textures.debug);
		else {
			/* Upload the decoded texhead data */
			hr->current.texture = surface;
			vk_surface_commit (surface);
#if 0
			/* DEBUG */
			vk_surface_draw (surface);
#endif
		}
	}
}

static void
upload_current_lightset (hikaru_renderer_t *hr)
{
	hikaru_gpu_lightset_t *ls = &hr->current.lightset;
	if (ls && !ls->uploaded) {
		ls->uploaded = 1;

		/* TODO */
		glDisable (GL_LIGHTING);
	}
}

/* This is called prior to drawing a mesh to update the currently tracked
 * state. */
static void
upload_current_state (hikaru_renderer_t *hr)
{
	upload_current_viewport (hr);
	upload_current_modelview (hr);
	upload_current_material (hr);
	upload_current_texhead (hr);
	upload_current_lightset (hr);
}

static void
free_mesh (hikaru_mesh_t *mesh)
{
	free (mesh->vertices);
	mesh->vertices = NULL;
	mesh->index = 0;
	mesh->tindex = 0;
	mesh->max = 0;
}

static void
free_meshes (hikaru_renderer_t *hr)
{
	unsigned i;
	for (i = 0; i < hr->meshes.index; i++)
		free_mesh (&hr->meshes.array[i]);
	free (hr->meshes.array);
	hr->meshes.array = NULL;
	hr->meshes.index = 0;
	hr->meshes.max = 0;

	hr->current.mesh = NULL;
}

#define COPY_VEC2F(d_, s_) \
		(d_)[0] = (s_)[0]; \
		(d_)[1] = (s_)[1];

#define COPY_VEC3F(d_, s_) \
		(d_)[0] = (s_)[0]; \
		(d_)[1] = (s_)[1]; \
		(d_)[2] = (s_)[2];

#define DEFAULT_NUM_VERTICES_PER_MESH	32

static void
mesh_ensure_vertex_buffer (hikaru_mesh_t *mesh)
{
	if (mesh->index+10 >= mesh->max) {
		unsigned n = MAX2 (DEFAULT_NUM_VERTICES_PER_MESH, mesh->max * 2);
		mesh->vertices = realloc (mesh->vertices, n * sizeof (hikaru_vertex_t));
		mesh->max = n;
	}
}

static void
mesh_add_position_normal_texcoords (hikaru_mesh_t *mesh, vec3f_t x, vec3f_t n, vec2f_t u)
{
	hikaru_vertex_t *vtx;

	VK_ASSERT (mesh);
	VK_ASSERT (mesh->type == MESH_TYPE_TRI_STRIP);

	mesh_ensure_vertex_buffer (mesh);

	vtx = &mesh->vertices[mesh->index];
	mesh->index++;

	COPY_VEC3F (vtx->pos, x);
	COPY_VEC3F (vtx->normal, n);
	COPY_VEC2F (vtx->texcoords, u);
}

static void
mesh_add_position (hikaru_mesh_t *mesh, vec3f_t x)
{
	hikaru_vertex_t *vtx;

	VK_ASSERT (mesh);
	VK_ASSERT (mesh->type == MESH_TYPE_TRI_LIST);

	mesh_ensure_vertex_buffer (mesh);

	vtx = &mesh->vertices[mesh->index];
	mesh->index++;

	memset (vtx, 0, sizeof (hikaru_vertex_t));
	COPY_VEC3F (vtx->pos, x);
}

static void
mesh_add_texcoords (hikaru_mesh_t *mesh, vec2f_t u[3])
{
	int i;

	VK_ASSERT (mesh);
	VK_ASSERT (mesh->type == MESH_TYPE_TRI_LIST);
	VK_ASSERT (mesh->index >= 3);

	if(mesh->index - mesh->tindex < 3) {
		int dt = mesh->index - mesh->tindex - 3;
		mesh_ensure_vertex_buffer (mesh);
		for(i=0; i<3; i++)
			mesh->vertices[mesh->tindex+(2-i)] = mesh->vertices[mesh->tindex+(2-i)+dt];
		mesh->index = mesh->tindex + 3;
	}
	for (i = 0; i < 3; i++) {
		mesh->vertices[mesh->tindex + i].texcoords[0] = u[2-i][0];
		mesh->vertices[mesh->tindex + i].texcoords[1] = u[2-i][1];
	}
	mesh->tindex = mesh->tindex + 3;
}

static void
mesh_draw (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	GLenum gl_type;
	float tmx = 1.0f, tmy = 1.0f;
	bool has_tex;
	unsigned i;

	has_tex = hr->current.material.has_texture && hr->current.texture;
	if (has_tex) {
		tmx = 1.0f / hr->current.texture->width;
		tmy = 1.0f / hr->current.texture->height;
	}

	gl_type = (mesh->type == MESH_TYPE_TRI_LIST) ?
	           GL_TRIANGLES : GL_TRIANGLE_STRIP;

	/* Push the vertices to GL */
	glBegin (gl_type);
	for (i = 0; i < mesh->index; i++) {
		hikaru_vertex_t *v = &mesh->vertices[i];

		if (has_tex)
			glTexCoord2f (v->texcoords[0]*tmx, v->texcoords[1]*tmy);
		if (mesh->type == MESH_TYPE_TRI_STRIP)
			glNormal3fv (v->normal);
		glVertex3fv (v->pos);
	}
	glEnd ();
}

static void
draw_current_mesh (hikaru_renderer_t *hr)
{
	hikaru_mesh_t *mesh = hr->current.mesh;
	if (!mesh)
		return;
	upload_current_state (hr);
	LOG ("drawing mesh: mode=%d, %d vertices", mesh->type, mesh->index);
	mesh_draw (hr, hr->current.mesh);
}

#define DEFAULT_MAX_MESHES 256

static void
resize_mesh_pool_if_needed (hikaru_renderer_t *hr)
{
	/* This condition should also be valid the first time new_mesh()
	 * is called, as realloc() acts as malloc() when its pointer
	 * argument is NULL. */
	if (hr->meshes.index >= hr->meshes.max) {
		unsigned n = MAX2 (DEFAULT_MAX_MESHES, hr->meshes.max * 2);
		LOG ("allocating mesh pool for %u meshes", n);
		hr->meshes.array = (hikaru_mesh_t *)
			realloc (hr->meshes.array, sizeof (hikaru_mesh_t) * n);
		VK_ASSERT (hr->meshes.array);
		hr->meshes.max = n;
	}
}

static hikaru_mesh_t *
add_mesh (hikaru_renderer_t *hr, hikaru_mesh_type_t type)
{
	hikaru_mesh_t *mesh;

	mesh = &hr->meshes.array[hr->meshes.index];
	mesh->type = type;
	mesh->vertices = NULL;
	mesh->index = 0;
	mesh->tindex = 0;
	mesh->max = 0;

	hr->meshes.index++;
	hr->current.mesh = mesh;
	return mesh;
}

static hikaru_mesh_t *
renderer_get_current_mesh (hikaru_renderer_t *hr,
                           hikaru_mesh_type_t type,
                           bool create_if_none)
{
	hikaru_mesh_t *mesh = hr->current.mesh;
	if (mesh && mesh->type != type) {
		VK_ABORT ("current mesh type is %u, requested mesh type is %u",
		          mesh->type, type);
	}
	if (!mesh && create_if_none) {
		resize_mesh_pool_if_needed (hr);
		mesh = add_mesh (hr, type);
	}
	return mesh;
}

void
hikaru_renderer_add_vertex_full (vk_renderer_t *renderer,
                                 vec3f_t pos,
                                 vec3f_t normal,
                                 vec2f_t texcoords)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	hikaru_mesh_t *mesh;
	if (hr->options.disable_3d)
		return;
	mesh = renderer_get_current_mesh (hr, MESH_TYPE_TRI_STRIP, true);
	VK_ASSERT (mesh);
	mesh_add_position_normal_texcoords (mesh, pos, normal, texcoords);
}

void
hikaru_renderer_add_vertex (vk_renderer_t *renderer, vec3f_t pos)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	hikaru_mesh_t *mesh;
	if (hr->options.disable_3d)
		return;
	mesh = renderer_get_current_mesh (hr, MESH_TYPE_TRI_LIST, true);
	VK_ASSERT (mesh);
	mesh_add_position (mesh, pos);
}

void
hikaru_renderer_add_texcoords (vk_renderer_t *renderer,
                                  vec2f_t texcoords[3])
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	hikaru_mesh_t *mesh; 

	if (hr->options.disable_3d)
		return;
	mesh = renderer_get_current_mesh (hr, MESH_TYPE_TRI_LIST, false);
	VK_ASSERT (mesh);
	mesh_add_texcoords (mesh, texcoords);
}

void
hikaru_renderer_end_mesh (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	if (hr->options.disable_3d)
		return;
	draw_current_mesh (hr);
	hr->current.mesh = NULL;
}

/* 2D Rendering */

static vk_surface_t *
decode_layer_rgba5551 (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;
	uint32_t x, y;

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA5551);
	if (!surface)
		return NULL;

	for (y = 0; y < 480; y++) {
		uint32_t yoffs = (layer->y0 + y) * 4096;
		for (x = 0; x < 640; x++) {
			uint32_t offs = yoffs + layer->x0 * 4 + x * 2;
			uint32_t texel = vk_buffer_get (hr->fb, 2, offs);
			vk_surface_put16 (surface, x ^ 1, y, texel);
		}
	}
	return surface;
}

static vk_surface_t *
decode_layer_rgba8888 (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;
	uint32_t x, y;

	surface = vk_surface_new (640, 480, VK_SURFACE_FORMAT_RGBA8888);
	if (!surface)
		return NULL;

	for (y = 0; y < 480; y++) {
		for (x = 0; x < 640; x++) {
			uint32_t offs = coords_to_offs_32 (layer->x0 + x, layer->y0 + y);
			uint32_t texel = vk_buffer_get (hr->fb, 4, offs);
			vk_surface_put32 (surface, x, y, texel);
		}
	}
	return surface;
}

static vk_surface_t *
upload_layer (hikaru_renderer_t *hr, hikaru_gpu_layer_t *layer)
{
	vk_surface_t *surface;

	if (layer->format == HIKARU_FORMAT_RGBA8888)
		surface = decode_layer_rgba8888 (hr, layer);
	else
		surface = decode_layer_rgba5551 (hr, layer);

	vk_surface_commit (surface);
	return surface;
}

void
hikaru_renderer_draw_layer (vk_renderer_t *renderer, hikaru_gpu_layer_t *layer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	vk_surface_t *surface;

	if (hr->options.disable_2d)
		return;

	LOG ("drawing layer %s", get_gpu_layer_str (layer));

	glDisable (GL_SCISSOR_TEST);

	/* XXX cache the layers and use dirty rectangles to upload only the
	 * quads that changed. */
	/* XXX change the renderer so that the ortho projection can be
	 * set up correctly depending on the actual window size. */

	surface = upload_layer (hr, layer);
	if (!surface) {
		VK_ERROR ("HR LAYER: can't upload layer to OpenGL, skipping");
		return;
	}
	vk_surface_draw (surface);
	vk_surface_destroy (&surface);
}

/* Debug */

static void
upload_fb (hikaru_renderer_t *hr)
{
	uint32_t x, y;
	VK_ASSERT (hr->textures.fb);
	for (y = 0; y < 2048; y++) {
		uint32_t yoffs = y * 4096;
		for (x = 0; x < 2048; x++) {
			uint32_t offs = yoffs + x * 2;
			uint32_t texel = vk_buffer_get (hr->fb, 2, offs);
			vk_surface_put16 (hr->textures.fb, x ^ 1, y, texel);
		}
	}
	vk_surface_commit (hr->textures.fb);
}

static void
upload_texram (hikaru_renderer_t *hr)
{
	uint32_t x, y;
	VK_ASSERT (hr->textures.texram);
	for (y = 0; y < 1024; y++) {
		uint32_t yoffs = y * 4096;
		for (x = 0; x < 2048; x++) {
			uint32_t texel, offs = yoffs + x * 2;
			texel = vk_buffer_get (hr->texram[0], 2, offs);
			vk_surface_put16 (hr->textures.texram, x ^ 1, y, texel);
			texel = vk_buffer_get (hr->texram[1], 2, offs);
			vk_surface_put16 (hr->textures.texram, 1024 + (x ^ 1), y, texel);
		}
	}
	vk_surface_commit (hr->textures.texram);
}

/* Interface */

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	/* Erase the current rendering state*/
	memset (&hr->current, 0, sizeof (hr->current));

	/* Delete all meshes (XXX actually cache them) */
	free_meshes (hr);

	/* clear the frame buffer to a bright pink color */
	glClearColor (1.0f, 0.0f, 1.0f, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Draw the FB and/or TEXRAM if asked to do so */
	if (hr->options.draw_fb) {
		upload_fb (hr);
		vk_surface_draw (hr->textures.fb);
	}
	if (hr->options.draw_texram) {
		upload_texram (hr);
		vk_surface_draw (hr->textures.texram);
	}
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	(void) renderer;
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	free_meshes (hr);
	free_textures (hr);
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;
		vk_surface_destroy (&hr->current.texture);
		vk_surface_destroy (&hr->textures.fb);
		vk_surface_destroy (&hr->textures.texram);
		vk_surface_destroy (&hr->textures.debug);
		free_meshes (hr);
		free_textures (hr);
	}
}

static vk_surface_t *
build_debug_surface (void)
{
	/* Build a colorful 2x2 checkerboard surface */
	vk_surface_t *surface = vk_surface_new (2, 2, VK_SURFACE_FORMAT_RGBA4444);
	if (!surface)
		return NULL;
	vk_surface_put16 (surface, 0, 0, 0xF00F);
	vk_surface_put16 (surface, 0, 1, 0xF0F0);
	vk_surface_put16 (surface, 1, 0, 0xFF00);
	vk_surface_put16 (surface, 1, 1, 0xFFFF);
	vk_surface_commit (surface);
	return surface;
}

static bool
build_default_surfaces (hikaru_renderer_t *hr)
{
	if (hr->options.draw_fb) {
		hr->textures.fb = vk_surface_new (
			2048, 2048, VK_SURFACE_FORMAT_RGBA5551);
		if (!hr->textures.fb) {
			VK_ERROR ("HR: failed to create FB surface, turning off");
			hr->options.draw_fb = false;
		}
	}

	if (hr->options.draw_texram) {
		hr->textures.texram = vk_surface_new (
			2048, 2048, VK_SURFACE_FORMAT_RGBA5551);
		if (!hr->textures.texram) {
			VK_ERROR ("HR: failed to create TEXRAM surface, turning off");
			hr->options.draw_texram = false;
		}
	}

	hr->textures.debug = build_debug_surface ();
	if (!hr->textures.debug)
		return false;

	return true;
}

vk_renderer_t *
hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2])
{
	hikaru_renderer_t *hr;
	int ret;

	hr = ALLOC (hikaru_renderer_t);
	if (!hr)
		goto fail;

	hr->base.width = 640;
	hr->base.height = 480;

	hr->base.destroy = hikaru_renderer_destroy;
	hr->base.reset = hikaru_renderer_reset;
	hr->base.begin_frame = hikaru_renderer_begin_frame;
	hr->base.end_frame = hikaru_renderer_end_frame;

	ret = vk_renderer_init ((vk_renderer_t *) hr);
	if (ret)
		goto fail;

	/* Setup machine buffers */
	hr->fb = fb;
	hr->texram[0] = texram[0];
	hr->texram[1] = texram[1];

	/* Read options from the environment */
	hr->options.log =
		vk_util_get_bool_option ("HR_LOG", false);
	hr->options.disable_2d =
		vk_util_get_bool_option ("HR_DISABLE_2D", false);
	hr->options.disable_3d =
		vk_util_get_bool_option ("HR_DISABLE_3D", false);
	hr->options.draw_fb =
		vk_util_get_bool_option ("HR_DRAW_FB", false);
	hr->options.draw_texram =
		vk_util_get_bool_option ("HR_DRAW_TEXRAM", false);
	hr->options.force_debug_texture =
		vk_util_get_bool_option ("HR_FORCE_DEBUG_TEXTURE", false);

	/* Create a few surfaces */
	if (!build_default_surfaces (hr))
		goto fail;

	return (vk_renderer_t *) hr;

fail:
	hikaru_renderer_destroy ((vk_renderer_t **) &hr);
	return NULL;
}
