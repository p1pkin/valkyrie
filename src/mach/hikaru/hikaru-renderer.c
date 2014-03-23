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

/* XXX implement mesh caching. requires storing color/texhead/vertices. */

#include "vk/input.h"

#include "mach/hikaru/hikaru-renderer.h"
#include "mach/hikaru/hikaru-renderer-private.h"

#define VP0	VP.scratch
#define MAT0	MAT.scratch
#define TEX0	TEX.scratch
#define LS0	LIT.scratchset

#define MAX_VIEWPORTS	4096
#define MAX_MODELVIEWS	4096
#define MAX_MATERIALS	4096
#define MAX_TEXHEADS	4096
#define MAX_LIGHTSETS	256
#define MAX_MESHES	16384

#define INV255	(1.0f / 255.0f)

/****************************************************************************
 Debug
****************************************************************************/

static const struct {
	int32_t min, max;
	uint32_t key;
	bool print;
	char name[32];
} debug_controls[] = {
	[HR_DEBUG_LOG]			= {  0, 1,     ~0, false, "LOG" },
	[HR_DEBUG_NO_LAYER1]		= {  0, 1, SDLK_1, false, "NO LAYER1" },
	[HR_DEBUG_NO_LAYER2]		= {  0, 1, SDLK_2, false, "NO LAYER2" },
	[HR_DEBUG_NO_3D]		= {  0, 1, SDLK_3, false, "NO 3D" },
	[HR_DEBUG_SELECT_BASE_COLOR]	= {  0, 9, SDLK_c,  true, "SELECT BASE COLOR" },
	[HR_DEBUG_SELECT_CULLFACE]	= { -1, 1, SDLK_f,  true, "SELECT CULLFACE" },
	[HR_DEBUG_NO_TEXTURES]		= {  0, 1, SDLK_t, false, "NO TEXTURES" },
	[HR_DEBUG_USE_DEBUG_TEXTURE]	= {  0, 1, SDLK_y, false, "USE DEBUG TEXTURE" },
	[HR_DEBUG_DUMP_TEXTURES]	= {  0, 1,     ~0, false, "DUMP TEXTURES" },
	[HR_DEBUG_DETWIDDLE_TEXTURES]	= {  0, 1, SDLK_u, false, "DETWIDDLE TEXTURES" },
	[HR_DEBUG_SELECT_POLYTYPE]	= { -1, 7, SDLK_p,  true, "SELECT POLYTYPE" },
	[HR_DEBUG_NO_INSTANCING]	= {  0, 1, SDLK_i, false, "NO INSTANCING" },
	[HR_DEBUG_SELECT_INSTANCE]	= {  0, 3, SDLK_j, false, "" },
	[HR_DEBUG_DRAW_NORMALS]		= {  0, 1, SDLK_n, false, "DRAW NORMALS" },
	[HR_DEBUG_NO_LIGHTING]		= {  0, 1, SDLK_l, false, "NO LIGHTING" },
	[HR_DEBUG_NO_AMBIENT]		= {  0, 1, SDLK_a, false, "NO AMBIENT" },
	[HR_DEBUG_NO_DIFFUSE]		= {  0, 1, SDLK_d, false, "NO DIFFUSE" },
	[HR_DEBUG_NO_SPECULAR]		= {  0, 1, SDLK_s, false, "NO SPECULAR" },
	[HR_DEBUG_SELECT_ATT_TYPE]	= { -1, 4, SDLK_z,  true, "SELECT ATT TYPE" },
};

static void
init_debug_flags (hikaru_renderer_t *hr)
{
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++)
		hr->debug.flags[i] = debug_controls[i].min;

	hr->debug.flags[HR_DEBUG_LOG] =
		vk_util_get_bool_option ("HR_LOG", false) ? 1 : 0;
	hr->debug.flags[HR_DEBUG_DUMP_TEXTURES] =
		vk_util_get_bool_option ("HR_DUMP_TEXTURES", false) ? 1 : 0;
}

static void
update_debug_flags (hikaru_renderer_t *hr)
{
	char *msg = hr->base.message;
	unsigned i;

	for (i = 0; i < NUMELEM (debug_controls); i++) {
		uint32_t key = debug_controls[i].key;
		if (key != ~0 && vk_input_get_key (key)) {
			hr->debug.flags[i] += 1;
			if (i == HR_DEBUG_DETWIDDLE_TEXTURES)
				hikaru_renderer_invalidate_texcache (&hr->base, NULL);
			if (hr->debug.flags[i] > debug_controls[i].max)
				hr->debug.flags[i] = debug_controls[i].min;
			if (debug_controls[i].print)
				VK_LOG ("HR DEBUG: '%s' = %d\n",
				        debug_controls[i].name, hr->debug.flags[i]);
		}
		msg += sprintf (msg, "%d|", hr->debug.flags[i]);
	}
}

/****************************************************************************
 Utils
****************************************************************************/

static void
print_uniforms (GLuint program)
{
	GLchar id[256];
	GLint count, loc, size, i;
	GLenum type;

	glGetProgramiv (program, GL_ACTIVE_UNIFORMS, &count);
	for (i = 0; i < count; i++) {
		glGetActiveUniform (program, i, sizeof (id), NULL, &size, &type, id);
		loc = glGetUniformLocation (program, id);
		VK_LOG ("uniform %u : %s <size %d>", loc, id, size);
	}
}

static GLuint
compile_shader (GLenum type, const char *src)
{
	char info[256];
	GLuint id;
	GLint status;

	id = glCreateShader (type);
	glShaderSource (id, 1, (const GLchar **) &src, NULL);
	glCompileShader (id);
	glGetShaderiv (id, GL_COMPILE_STATUS, &status);
	if (!status) {
		glGetShaderInfoLog (id, sizeof (info), NULL, info);
		VK_ERROR ("could not compile GLSL shader: '%s'\n", info);
		VK_ERROR ("source:\n%s\n", src);
		glDeleteShader (id);
		VK_ASSERT (0);
	}
	return id;
}

static GLuint
compile_program (const char *vs_src, const char *fs_src)
{
	char info[256];
	GLuint id, vs, fs;
	GLint status;

	vs = compile_shader (GL_VERTEX_SHADER, vs_src);
	VK_ASSERT_NO_GL_ERROR ();

	fs = compile_shader (GL_FRAGMENT_SHADER, fs_src);
	VK_ASSERT_NO_GL_ERROR ();

	id = glCreateProgram ();
	glAttachShader (id, vs);
	glAttachShader (id, fs);
	glLinkProgram (id);
	glGetProgramiv (id, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) {
		glGetProgramInfoLog (id, sizeof (info), NULL, info);
		VK_ERROR ("could not link GLSL program: '%s'\n", info);
		VK_ERROR ("vs source:\n%s\n", vs_src);
		VK_ERROR ("fs source:\n%s\n", fs_src);
		glDeleteProgram (id);
		VK_ASSERT (0);
	}

	/* "If a shader object to be deleted is attached to a program object,
	 * it will be flagged for deletion, but it will not be deleted until it
	 * is no longer attached to any program object, for any rendering
	 * context." */
	glDeleteShader (vs);
	glDeleteShader (fs);
	VK_ASSERT_NO_GL_ERROR ();

	return id;
}

static void
destroy_program (GLuint program)
{
	if (program) {
		glUseProgram (0);
		glDeleteProgram (program);
	}
}

static void
ortho (mtx4x4f_t proj, float l, float r, float b, float t, float n, float f)
{
	proj[0][0] = 2.0f / (r - l);
	proj[1][0] = 0.0f;
	proj[2][0] = 0.0f;
	proj[3][0] = - (r + l) / (r - l);

	proj[0][1] = 0.0f;
	proj[1][1] = 2.0f / (t - b);
	proj[2][1] = 0.0f;
	proj[3][1] = - (t + b) / (t - b);

	proj[0][2] = 0.0f;
	proj[1][2] = 0.0f;
	proj[2][2] = -2.0f / (f - n);
	proj[3][2] = - (f + n) / (f - n);

	proj[0][3] = 0.0f;
	proj[1][3] = 0.0f;
	proj[2][3] = 0.0f;
	proj[3][3] = 1.0f;
}

static void
frustum (mtx4x4f_t proj, float l, float r, float b, float t, float n, float f)
{
	proj[0][0] = (2.0f * n) / (r - l);
	proj[1][0] = 0.0f;
	proj[2][0] = (r + l) / (r - l);
	proj[3][0] = 0.0f;

	proj[0][1] = 0.0f;
	proj[1][1] = (2.0f * n) / (t - b);
	proj[2][1] = (t + b) / (t - b);
	proj[3][1] = 0.0f;

	proj[0][2] = 0.0f;
	proj[1][2] = 0.0f;
	proj[2][2] = - (f + n) / (f - n);
	proj[3][2] = - (2.0f * f * n) / (f - n);

	proj[0][3] = 0.0f;
	proj[1][3] = 0.0f;
	proj[2][3] = -1.0f;
	proj[3][3] = 0.0f;
}

static void
translate (mtx4x4f_t m, float x, float y, float z)
{
	m[3][0] += m[0][0] * x + m[1][0] * y + m[2][0] * z;
	m[3][1] += m[0][1] * x + m[1][1] * y + m[2][1] * z;
	m[3][2] += m[0][2] * x + m[1][2] * y + m[2][2] * z;
	m[3][3] += m[0][3] * x + m[1][3] * y + m[2][3] * z;
}

/****************************************************************************
 State
****************************************************************************/

static const char *mesh_vs_source =
"#version 140									\n \
										\n \
%s										\n \
										\n \
uniform mat4 u_projection;							\n \
uniform mat4 u_modelview;							\n \
uniform mat3 u_normal;								\n \
										\n \
in vec4 i_diffuse;								\n \
in vec4 i_specular;								\n \
in vec3 i_position;								\n \
in vec3 i_normal;								\n \
in vec3 i_ambient;								\n \
in vec3 i_unknown;								\n \
in vec2 i_texcoords;								\n \
										\n \
out vec4 p_position;								\n \
out vec3 p_normal;								\n \
out vec4 p_diffuse;								\n \
out vec4 p_specular;								\n \
out vec3 p_ambient;								\n \
out vec2 p_texcoords;								\n \
										\n \
void main (void) {								\n \
	p_position = u_modelview * vec4 (i_position, 1.0);			\n \
	gl_Position = u_projection * p_position;				\n \
										\n \
	mat3 normal_matrix = mat3 (transpose (inverse (u_modelview)));		\n \
	p_normal = normalize (normal_matrix * i_normal);			\n \
										\n \
	p_diffuse = i_diffuse;							\n \
	p_ambient = i_ambient;							\n \
	p_specular = i_specular;						\n \
	p_texcoords = i_texcoords;						\n \
}";

static const char *mesh_fs_source =
"#version 140									\n \
										\n \
%s										\n \
										\n \
struct light_t {								\n \
	vec3 position;								\n \
	vec3 direction;								\n \
	vec3 diffuse;								\n \
	vec3 specular;								\n \
	vec2 extents;								\n \
};										\n \
										\n \
uniform light_t		u_lights[4];						\n \
uniform vec3		u_ambient;						\n \
uniform sampler2D	u_texture;						\n \
										\n \
in vec4 p_position;								\n \
in vec3 p_normal;								\n \
in vec4 p_diffuse;								\n \
in vec4 p_specular;								\n \
in vec3 p_ambient;								\n \
in vec2 p_texcoords;								\n \
										\n \
void										\n \
apply_light (inout vec4 color, in light_t light, in int type, in int att_type)	\n \
{										\n \
	vec3 light_direction;							\n \
	float distance, attenuation, intensity;					\n \
										\n \
	if (type == 0) {							\n \
		light_direction = normalize (light.direction);			\n \
		distance = 0.0;							\n \
	} else {								\n \
		vec3 delta = light.position - p_position.xyz;			\n \
		distance = length (delta);					\n \
		light_direction = normalize (delta);				\n \
	}									\n \
	attenuation = 1.0;							\n \
	if (att_type == 0) {							\n \
		float a = light.extents.x * (light.extents.y + distance);	\n \
		attenuation = clamp (a, 0.0, 1.0);				\n \
	} else if (att_type == 1) {						\n \
		float a = light.extents.x * (light.extents.y + distance*distance); \n \
		attenuation = clamp (a, 0.0, 1.0);				\n \
	} else if (att_type == 2) {						\n \
		float a = light.extents.x * (light.extents.y - 1 / distance);	\n \
		attenuation = clamp (a, 0.0, 1.0);				\n \
	} else if (att_type == 3) {						\n \
		float a = light.extents.x * (light.extents.y - 1 / (distance*distance)); \n \
		attenuation = clamp (a, 0.0, 1.0);				\n \
	}									\n \
	intensity = max (dot (p_normal, light_direction), 0.0);			\n \
	if (type == 2) {							\n \
		vec3 spot_direction = normalize (light.direction);		\n \
		if (dot (spot_direction, light_direction) < 0.95)		\n \
			intensity = 0.0;					\n \
	}									\n \
	color += attenuation * intensity *					\n \
		 p_diffuse * vec4 (light.diffuse, 1.0);				\n \
}										\n \
										\n \
void										\n \
main (void)									\n \
{										\n \
#if HAS_LIGHTING								\n \
	vec3 light_direction, spot_direction;					\n \
	float light_distance, attenuation, intensity;				\n \
										\n \
	vec4 color = vec4 (u_ambient * p_ambient, 0.0);				\n \
										\n \
#if HAS_LIGHT0									\n \
	apply_light (color, u_lights[0], LIGHT0_TYPE, LIGHT0_ATT_TYPE);		\n \
#endif										\n \
#if HAS_LIGHT1									\n \
	apply_light (color, u_lights[1], LIGHT1_TYPE, LIGHT1_ATT_TYPE);		\n \
#endif										\n \
#if HAS_LIGHT2									\n \
	apply_light (color, u_lights[2], LIGHT2_TYPE, LIGHT2_ATT_TYPE);		\n \
#endif										\n \
#if HAS_LIGHT3									\n \
	apply_light (color, u_lights[3], LIGHT3_TYPE, LIGHT3_ATT_TYPE);		\n \
#endif										\n \
										\n \
	gl_FragColor = color;							\n \
#else										\n \
	gl_FragColor = p_diffuse;						\n \
#endif										\n \
}";

static hikaru_light_att_t
get_light_attenuation_type (hikaru_light_t *lit)
{
	if (lit->att_type == 0 &&
	    lit->attenuation[0] == 1.0f &&
	    lit->attenuation[1] == 1.0f)
		return HIKARU_LIGHT_ATT_INF;
	return lit->att_type;
}

static hikaru_light_type_t
get_light_type (hikaru_light_t *lit)
{
	VK_ASSERT (lit->has_direction || lit->has_position);
	if (get_light_attenuation_type (lit) == HIKARU_LIGHT_ATT_INF)
		return HIKARU_LIGHT_TYPE_DIRECTIONAL;
	else if (lit->has_direction && lit->has_position)
		return HIKARU_LIGHT_TYPE_SPOT;
	else if (lit->has_position)
		return HIKARU_LIGHT_TYPE_POSITIONAL;
	return HIKARU_LIGHT_TYPE_DIRECTIONAL;
}

static hikaru_glsl_variant_t
get_glsl_variant (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_material_t *mat	= &hr->mat_list[mesh->mat_index];
	hikaru_lightset_t *ls	= &hr->ls_list[mesh->ls_index];
	hikaru_glsl_variant_t variant;

	VK_ASSERT (mat);
	VK_ASSERT (ls);

	variant.has_texture		= mat->has_texture &&
	                        	  !hr->debug.flags[HR_DEBUG_NO_TEXTURES];

	variant.has_lighting		= ls->mask != 0xF &&
//	                        	  mat->shading_mode != 0 &&
	                                  !hr->debug.flags[HR_DEBUG_NO_LIGHTING];

	variant.has_phong		= ls->mask != 0xF &&
	                                  mat->shading_mode == 2 &&
	                                  !hr->debug.flags[HR_DEBUG_NO_LIGHTING];

//		if (hr->debug.flags[HR_DEBUG_SELECT_ATT_TYPE] >= 0 &&
//		    hr->debug.flags[HR_DEBUG_SELECT_ATT_TYPE] != get_light_attenuation_type (lt))
//			continue;

	variant.has_light0		= !(ls->mask & (1 << 0));
	variant.light0_type		= get_light_type (&ls->lights[0]);
	variant.light0_att_type		= get_light_attenuation_type (&ls->lights[0]);
	variant.has_light0_specular	= ls->lights[0].has_specular;

	variant.has_light1		= !(ls->mask & (1 << 1));
	variant.light1_type		= get_light_type (&ls->lights[1]);
	variant.light1_att_type		= get_light_attenuation_type (&ls->lights[1]);
	variant.has_light1_specular	= ls->lights[1].has_specular;

	variant.has_light2		= !(ls->mask & (1 << 2));
	variant.light2_type		= get_light_type (&ls->lights[2]);
	variant.light2_att_type		= get_light_attenuation_type (&ls->lights[2]);
	variant.has_light2_specular	= ls->lights[2].has_specular;

	variant.has_light3		= !(ls->mask & (1 << 3));
	variant.light3_type		= get_light_type (&ls->lights[3]);
	variant.light3_att_type		= get_light_attenuation_type (&ls->lights[3]);
	variant.has_light3_specular	= ls->lights[3].has_specular;

	return variant;
}

#define MAX_PROGRAMS	256

static struct {
	hikaru_glsl_variant_t variant;
	GLuint program;
} program_cache[MAX_PROGRAMS];
unsigned num_programs;

static void
upload_glsl_program (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	static const char *definitions_template =
	"#define HAS_TEXTURE %d\n"
	"#define HAS_LIGHTING %d\n"
	"#define HAS_PHONG %d\n"
	"#define HAS_LIGHT0 %d\n"
	"#define LIGHT0_TYPE %d\n"
	"#define LIGHT0_ATT_TYPE %d\n"
	"#define HAS_LIGHT0_SPECULAR %d\n"
	"#define HAS_LIGHT1 %d\n"
	"#define LIGHT1_TYPE %d\n"
	"#define LIGHT1_ATT_TYPE %d\n"
	"#define HAS_LIGHT1_SPECULAR %d\n"
	"#define HAS_LIGHT2 %d\n"
	"#define LIGHT2_TYPE %d\n"
	"#define LIGHT2_ATT_TYPE %d\n"
	"#define HAS_LIGHT2_SPECULAR %d\n"
	"#define HAS_LIGHT3 %d\n"
	"#define LIGHT3_TYPE %d\n"
	"#define LIGHT3_ATT_TYPE %d\n"
	"#define HAS_LIGHT3_SPECULAR %d\n";

	hikaru_glsl_variant_t variant;
	char *definitions, *vs_source, *fs_source;
	int ret, i;

	variant = get_glsl_variant (hr, mesh);
	if (hr->meshes.variant.full == variant.full)
		return;

	hr->meshes.variant.full = variant.full;

	for (i = 0; i < num_programs; i++) {
		if (program_cache[i].variant.full == variant.full) {
			hr->meshes.program = program_cache[i].program;
			goto update_locations;
		}
	}

	VK_LOG ("compiling shader for variant %X", variant.full);

//	destroy_program (hr->meshes.program);
//	VK_ASSERT_NO_GL_ERROR ();

	ret = asprintf (&definitions, definitions_template,
	                variant.has_texture,
	                variant.has_lighting,
	                variant.has_phong,
	                variant.has_light0,
	                variant.light0_type,
	                variant.light0_att_type,
	                variant.has_light0_specular,
	                variant.has_light1,
	                variant.light1_type,
	                variant.light1_att_type,
	                variant.has_light1_specular,
	                variant.has_light2,
	                variant.light2_type,
	                variant.light2_att_type,
	                variant.has_light2_specular,
	                variant.has_light3,
	                variant.light3_type,
	                variant.light3_att_type,
	                variant.has_light3_specular);
	VK_ASSERT (ret >= 0);

	ret = asprintf (&vs_source, mesh_vs_source, definitions);
	VK_ASSERT (ret >= 0);

	ret = asprintf (&fs_source, mesh_fs_source, definitions);
	VK_ASSERT (ret >= 0);

	hr->meshes.variant = variant;
	hr->meshes.program = compile_program (vs_source, fs_source);
	VK_ASSERT_NO_GL_ERROR ();

	free (definitions);
	free (vs_source);
	free (fs_source);

	if (0) {
		print_uniforms (hr->meshes.program);
		VK_ASSERT_NO_GL_ERROR ();
	}

	program_cache[num_programs].variant.full = variant.full;
	program_cache[num_programs].program = hr->meshes.program;
	num_programs++;
	VK_ASSERT (num_programs < MAX_PROGRAMS);

update_locations:

	glUseProgram (hr->meshes.program);
	VK_ASSERT_NO_GL_ERROR ();

	hr->meshes.locs.u_projection =
		glGetUniformLocation (hr->meshes.program, "u_projection");
	hr->meshes.locs.u_modelview =
		glGetUniformLocation (hr->meshes.program, "u_modelview");
	hr->meshes.locs.u_normal =
		glGetUniformLocation (hr->meshes.program, "u_normal");
	for (i = 0; i < 4; i++) {
		char temp[64];

		sprintf (temp, "u_lights[%d].position", i);
		hr->meshes.locs.u_lights[i].position =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].direction", i);
		hr->meshes.locs.u_lights[i].direction =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].diffuse", i);
		hr->meshes.locs.u_lights[i].diffuse =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].specular", i);
		hr->meshes.locs.u_lights[i].specular =
			glGetUniformLocation (hr->meshes.program, temp);
		sprintf (temp, "u_lights[%d].extents", i);
		hr->meshes.locs.u_lights[i].extents =
			glGetUniformLocation (hr->meshes.program, temp);
	}
	hr->meshes.locs.u_ambient =
		glGetUniformLocation (hr->meshes.program, "u_ambient");
	hr->meshes.locs.u_texture =
		glGetUniformLocation (hr->meshes.program, "u_texture");
	VK_ASSERT_NO_GL_ERROR ();

	hr->meshes.locs.i_position =
		glGetAttribLocation (hr->meshes.program, "i_position");
	hr->meshes.locs.i_normal =
		glGetAttribLocation (hr->meshes.program, "i_normal");
	hr->meshes.locs.i_texcoords =
		glGetAttribLocation (hr->meshes.program, "i_texcoords");
	hr->meshes.locs.i_diffuse =
		glGetAttribLocation (hr->meshes.program, "i_diffuse");
	hr->meshes.locs.i_ambient =
		glGetAttribLocation (hr->meshes.program, "i_ambient");
	hr->meshes.locs.i_specular =
		glGetAttribLocation (hr->meshes.program, "i_specular");
	hr->meshes.locs.i_unknown =
		glGetAttribLocation (hr->meshes.program, "i_unknown");
	VK_ASSERT_NO_GL_ERROR ();
}

static void
destroy_3d_glsl_state (hikaru_renderer_t *hr)
{
	destroy_program (hr->meshes.program);
	VK_ASSERT_NO_GL_ERROR ();

	if (hr->meshes.vao) {
		glBindVertexArray (0);
		glDeleteVertexArrays (1, &hr->meshes.vao);
	}
}

static void
upload_viewport (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_viewport_t *vp = &hr->vp_list[mesh->vp_index];
	const float h = vp->clip.t - vp->clip.b;
	const float w = vp->clip.r - vp->clip.l;
	const float hh_at_n = (h / 2.0f) * (vp->clip.n / vp->clip.f);
	const float hw_at_n = hh_at_n * (w / h);
	const float dcx = vp->offset.x - (w / 2.0f);
	const float dcy = vp->offset.y - (h / 2.0f);
	mtx4x4f_t projection;

	VK_ASSERT (mesh->vp_index != ~0);

	if (!ispositive (vp->clip.l) || !ispositive (vp->clip.r) ||
	    !ispositive (vp->clip.b) || !ispositive (vp->clip.t) ||
	    !ispositive (vp->clip.f) || !ispositive (vp->clip.n)) {
		VK_ERROR ("non-positive viewport clipping planes: %s",
		          get_viewport_str (vp));
		/* continue anyway */
	}

	if ((vp->clip.l >= vp->clip.r) ||
	    (vp->clip.b >= vp->clip.t) ||
	    (vp->clip.n >= vp->clip.f)) {
		VK_ERROR ("inverted viewport clipping planes: %s",
		          get_viewport_str (vp));
		/* continue anyway */
	}

	if (!ispositive (vp->offset.x) || (vp->offset.x >= 640.0f) ||
	    !ispositive (vp->offset.y) || (vp->offset.y >= 480.0f)) {
		VK_ERROR ("invalid viewport offset: %s",
		          get_viewport_str (vp));
		/* continue anyway */
	}

	LOG ("vp  = %s : [w=%f h=%f dcx=%f dcy=%f]",
	     get_viewport_str (vp), w, h, dcx, dcy);

	frustum (projection, -hw_at_n, hw_at_n, -hh_at_n, hh_at_n, vp->clip.n, 1e5);
	translate (projection, dcx, -dcy, 0.0f);

	glUniformMatrix4fv (hr->meshes.locs.u_projection, 1, GL_FALSE,
	                    (const GLfloat *) projection);
}

static void
upload_modelview (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, unsigned i)
{
	hikaru_modelview_t *mv = &hr->mv_list[mesh->mv_index + i];

	VK_ASSERT (mesh->mv_index != ~0);

	LOG ("mv  = [%u+%u] %s", mesh->mv_index, i, get_modelview_str (mv));

	glUniformMatrix4fv (hr->meshes.locs.u_modelview, 1, GL_FALSE,
	                    (const GLfloat *) mv->mtx);
}

static void
upload_material_texhead (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_material_t *mat = &hr->mat_list[mesh->mat_index];
	hikaru_texhead_t *th = &hr->tex_list[mesh->tex_index];
	bool has_texture;

	VK_ASSERT (mesh->mat_index != ~0);
	VK_ASSERT (mesh->tex_index != ~0);

	has_texture = mat->has_texture && !hr->debug.flags[HR_DEBUG_NO_TEXTURES];

	/* TODO decode the texture and upload the uniform ith glUniform1i. */
	(void) mat;
	(void) th;
	(void) has_texture;
}

static void
get_light_ambient (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, float *out)
{
	hikaru_viewport_t *vp =
		(mesh->vp_index == ~0) ? NULL : &hr->vp_list[mesh->vp_index];

	if (hr->debug.flags[HR_DEBUG_NO_AMBIENT] || !vp)
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = vp->color.ambient[0] * INV255;
		out[1] = vp->color.ambient[1] * INV255;
		out[2] = vp->color.ambient[2] * INV255;
	}
}

static void
get_light_diffuse (hikaru_renderer_t *hr, hikaru_light_t *lit, float *out)
{
	if (hr->debug.flags[HR_DEBUG_NO_DIFFUSE])
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = lit->diffuse[0] * INV255;
		out[1] = lit->diffuse[1] * INV255;
		out[2] = lit->diffuse[2] * INV255;
	}
}

static void
get_light_specular (hikaru_renderer_t *hr, hikaru_light_t *lit, float *out)
{
	if (!lit->has_specular || hr->debug.flags[HR_DEBUG_NO_SPECULAR])
		out[0] = out[1] = out[2] = 0.0f;
	else {
		out[0] = lit->specular[0] * INV255;
		out[1] = lit->specular[1] * INV255;
		out[2] = lit->specular[2] * INV255;
	}
}

static void
upload_lightset (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_material_t *mat = &hr->mat_list[mesh->mat_index];
	hikaru_lightset_t *ls = &hr->ls_list[mesh->ls_index];
	float tmp[4];
	unsigned i;

	VK_ASSERT (mesh->mat_index != ~0);
	VK_ASSERT (mesh->ls_index != ~0);

	LOG ("lightset = %s", get_lightset_str (ls));

	if (ls->mask == 0xF) {
		VK_ERROR ("uploading lightset with no light, skipping");
		return;
	}

	if (hr->debug.flags[HR_DEBUG_NO_LIGHTING] || mat->shading_mode == 0)
		return;

	get_light_ambient (hr, mesh, tmp);
	glUniform3fv (hr->meshes.locs.u_ambient, 1, (const GLfloat *) tmp);
	VK_ASSERT_NO_GL_ERROR ();

	for (i = 0; i < 4; i++) {
		hikaru_light_t *lt = &ls->lights[i];

		if (ls->mask & (1 << i))
			continue;

		glUniform3fv (hr->meshes.locs.u_lights[i].position, 1,
		              lt->position);
		VK_ASSERT_NO_GL_ERROR ();

		glUniform3fv (hr->meshes.locs.u_lights[i].direction, 1,
		              lt->direction);
		VK_ASSERT_NO_GL_ERROR ();

		get_light_diffuse (hr, lt, tmp);
		glUniform3fv (hr->meshes.locs.u_lights[i].diffuse, 1, tmp);
		VK_ASSERT_NO_GL_ERROR ();

		get_light_specular (hr, lt, tmp);
		glUniform3fv (hr->meshes.locs.u_lights[i].specular, 1, tmp);
		VK_ASSERT_NO_GL_ERROR ();

		glUniform2fv (hr->meshes.locs.u_lights[i].extents, 1,
		              lt->attenuation);
	}
}

/****************************************************************************
 Meshes
****************************************************************************/

#define VK_COPY_VEC2F(dst_, src_) \
	do { \
		dst_[0] = src_[0]; \
		dst_[1] = src_[1]; \
	} while (0)

#define VK_COPY_VEC3F(dst_, src_) \
	do { \
		dst_[0] = src_[0]; \
		dst_[1] = src_[1]; \
		dst_[2] = src_[2]; \
	} while (0)

static float
clampf (float x, float min_, float max_)
{
	return (min_ > x) ? min_ :
	       (max_ < x) ? max_ : x;
}

static void
copy_colors (hikaru_renderer_t *hr, hikaru_vertex_t *dst, hikaru_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_material_t *mat = &MAT.scratch;
	unsigned i;
	float alpha;

	for (i = 0; i < 4; i++)
		dst->diffuse[i] = mat->diffuse[i] * INV255;

	for (i = 0; i < 3; i++)
		dst->ambient[i] = mat->ambient[i] * INV255;

	for (i = 0; i < 4; i++)
		dst->specular[i] = mat->specular[i] * INV255;

	for (i = 0; i < 3; i++)
		dst->unknown[i] = mat->unknown[i] * INV255;

	/* Patch diffuse alpha depending on poly type. */
	alpha = 1.0f;
	if (POLY.type == HIKARU_POLYTYPE_TRANSLUCENT) {
		float p_alpha = POLY.alpha;
		//float m_alpha = mat->diffuse[3] * INV255;
		float v_alpha = src->info.alpha * INV255;
		alpha = clampf (p_alpha * v_alpha, 0.0f, 1.0f);
	}
	dst->diffuse[3] = alpha;
}

static void
copy_texcoords (hikaru_renderer_t *hr,
                hikaru_vertex_t *dst, hikaru_vertex_t *src)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_texhead_t *th = &TEX.scratch;
	float w = 16 << th->logw;
	float h = 16 << th->logh;

	if (th->format == HIKARU_FORMAT_ABGR1111)
		h *= 2;

	dst->texcoords[0] = src->texcoords[0] / w;
	dst->texcoords[1] = src->texcoords[1] / h;
}

static void
add_triangle (hikaru_renderer_t *hr)
{
	if (hr->push.num_verts >= 3) {
		uint32_t index = hr->push.num_tris * 3;
		hikaru_vertex_t *dst = &hr->push.all[index];

		VK_ASSERT ((index + 2) < MAX_VERTICES_PER_MESH);

		if (hr->push.tmp[2].info.winding) {
			dst[0] = hr->push.tmp[0];
			dst[1] = hr->push.tmp[2];
			dst[2] = hr->push.tmp[1];
		} else {
			dst[0] = hr->push.tmp[0];
			dst[1] = hr->push.tmp[1];
			dst[2] = hr->push.tmp[2];
		}
		hr->push.num_tris += 1;
	}
}

void
hikaru_renderer_push_vertices (vk_renderer_t *rend,
                               hikaru_vertex_t *v,
                               uint32_t flags,
                               unsigned num)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	unsigned i;

	VK_ASSERT (hr);
	VK_ASSERT (v);
	VK_ASSERT (num == 1 || num == 3);
	VK_ASSERT (v->info.tricap == 0 || v->info.tricap == 7);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	switch (num) {

	case 1:
		/* Note that VTX(2) always points to the last pushed vertex,
		 * which for instructions 12x, 1Ax and 1Bx means the vertex
		 * pushed by the instruction itself, and for instructions 1Ex
		 * and 15x the vertex pushed by the previous "push"
		 * instruction.
		 */

		/* If the incoming vertex includes the position, push it
		 * in the temporary buffer, updating it according to the
		 * p(osition)pivot bit. */
		if (flags & HR_PUSH_POS) {

			/* Do not change the pivot if it is not required */
			if (!v->info.ppivot)
				hr->push.tmp[0] = hr->push.tmp[1];
			hr->push.tmp[1] = hr->push.tmp[2];
			memset ((void *) &hr->push.tmp[2], 0, sizeof (hikaru_vertex_t));

			/* Set the position, colors and alpha. */
			VK_COPY_VEC3F (hr->push.tmp[2].position, v->position);
			copy_colors (hr, &hr->push.tmp[2], v);

			/* Account for the added vertex. */
			hr->push.num_verts += 1;
			VK_ASSERT (hr->push.num_verts < MAX_VERTICES_PER_MESH);
		}

		/* Set the normal. */
		if (flags & HR_PUSH_NRM)
			VK_COPY_VEC3F (hr->push.tmp[2].normal, v->normal);

		/* Set the texcoords. */
		if (flags & HR_PUSH_TXC)
			copy_texcoords (hr, &hr->push.tmp[2], v);
		break;

	case 3:
		VK_ASSERT (flags == HR_PUSH_TXC);

		if (hr->push.num_verts < 3)
			return;

		for (i = 0; i < 3; i++)
			copy_texcoords (hr, &hr->push.tmp[2 - i], &v[i]);
		break;

	default:
		VK_ASSERT (!"num is neither 1 nor 3");
		break;
	}

	/* Finish the previous triangle. */
	if (v[0].info.tricap == 7) {
		hr->push.tmp[2].info.full = v[0].info.full;
		add_triangle (hr);
	}
}

static void
print_rendstate (hikaru_renderer_t *hr, hikaru_mesh_t *mesh, char *prefix)
{
	LOG ("RENDSTATE %s @%p #instances = %u", prefix, mesh, mesh->num_instances);
	if (mesh->vp_index < MAX_VIEWPORTS)
		LOG ("RENDSTATE %s %u vp:  %s", prefix, mesh->num,
		     get_viewport_str (&hr->vp_list[mesh->vp_index]));
	if (mesh->mv_index < MAX_MODELVIEWS)
		LOG ("RENDSTATE %s %u mv:  %s", prefix, mesh->num,
		     get_modelview_str (&hr->mv_list[mesh->mv_index]));
	if (mesh->mat_index < MAX_MATERIALS)
		LOG ("RENDSTATE %s %u mat: %s", prefix, mesh->num,
		     get_material_str (&hr->mat_list[mesh->mat_index]));
	if (mesh->tex_index < MAX_TEXHEADS)
		LOG ("RENDSTATE %s %u tex: %s", prefix, mesh->num,
		     get_texhead_str (&hr->tex_list[mesh->tex_index]));
	if (mesh->ls_index < MAX_LIGHTSETS)
		LOG ("RENDSTATE %s %u ls:  %s", prefix, mesh->num,
		     get_lightset_str (&hr->ls_list[mesh->ls_index]));
}

/* TODO check if more fine-grained uploaded tracking can help. */
/* TODO check boundary conditions when nothing is uploaded in a frame. */
/* TODO alpha threshold */
static void
update_and_set_rendstate (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned i;

	if (VP0.uploaded) {
		LOG ("RENDSTATE updating vp %u/%u", hr->num_vps, MAX_VIEWPORTS);
		hr->vp_list[hr->num_vps++] = VP0;
		VK_ASSERT (hr->num_vps < MAX_VIEWPORTS);
		VP0.uploaded = 0;
	}
	if (MAT0.uploaded) {
		LOG ("RENDSTATE updating mat %u/%u", hr->num_mats, MAX_MATERIALS);
		hr->mat_list[hr->num_mats++] = MAT0;
		VK_ASSERT (hr->num_mats < MAX_MATERIALS);
		MAT0.uploaded = 0;
	}
	if (TEX0.uploaded) {
		LOG ("RENDSTATE updating tex %u/%u", hr->num_texs, MAX_TEXHEADS);
		hr->tex_list[hr->num_texs++] = TEX0;
		VK_ASSERT (hr->num_texs < MAX_TEXHEADS);
		TEX0.uploaded = 0;
	}
	if (LS0.uploaded) {
		LOG ("RENDSTATE updating ls %u/%u", hr->num_lss, MAX_LIGHTSETS);
		hr->ls_list[hr->num_lss++] = LS0;
		VK_ASSERT (hr->num_lss < MAX_LIGHTSETS);
		LS0.uploaded = 0;
	}

	/* Copy the per-instance modelviews from last to first. */
	/* TODO optimize by setting MV.total to 0 (and fix the fallback). */
	if (MV.total == 0) {
		LOG ("RENDSTATE adding no mvs %u/%u [#instances=%u]",
		     hr->num_mvs, MAX_MODELVIEWS, hr->num_instances);

		mesh->mv_index = hr->num_mvs - 1;
		mesh->num_instances = hr->num_instances;
	} else {
		mesh->mv_index = hr->num_mvs;
		mesh->num_instances = MV.total;
		hr->num_instances = MV.total;

		for (i = 0; i < MV.total; i++, hr->num_mvs++) {
			LOG ("RENDSTATE adding mv %u/%u [#instances=%u]",
			     hr->num_mvs, MAX_MODELVIEWS, MV.total);
	
			VK_ASSERT (hr->num_mvs < MAX_MODELVIEWS);
			hr->mv_list[hr->num_mvs] = MV.table[i];
		}

		MV.total = 0;
		MV.depth = 0;
	}

	mesh->vp_index = hr->num_vps - 1;
	mesh->mat_index = hr->num_mats - 1;
	mesh->tex_index = hr->num_texs - 1;
	mesh->ls_index = hr->num_lss - 1;

	mesh->num = hr->total_meshes++;
	print_rendstate (hr, mesh, "U");
}

#define OFFSET(member_) \
	((const GLvoid *) offsetof (hikaru_vertex_t, member_))

#define VAP(loc_, num_, type_, member_) \
	if (loc_ != (GLuint) -1) { \
		glVertexAttribPointer (loc_, num_, type_, GL_FALSE, \
		                       sizeof (hikaru_vertex_t), \
		                       OFFSET (member_)); \
		VK_ASSERT_NO_GL_ERROR (); \
		\
		glEnableVertexAttribArray (loc_); \
		VK_ASSERT_NO_GL_ERROR (); \
	}

static void
draw_mesh (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	unsigned i;

	VK_ASSERT (mesh);
	VK_ASSERT (mesh->vbo);

	LOG ("==== DRAWING MESH @%p (#vertices=%u #instances=%u) ====",
	     mesh, mesh->num_tris * 3, mesh->num_instances);

	print_rendstate (hr, mesh, "D");

	glBindVertexArray (hr->meshes.vao);
	VK_ASSERT_NO_GL_ERROR ();

	glBindBuffer (GL_ARRAY_BUFFER, mesh->vbo);
	VK_ASSERT_NO_GL_ERROR ();

	upload_glsl_program (hr, mesh);
	VK_ASSERT_NO_GL_ERROR ();

	upload_viewport (hr, mesh);
	upload_material_texhead (hr, mesh);
	upload_lightset (hr, mesh);
	VK_ASSERT_NO_GL_ERROR ();

	/* We must do it here since locs are computed in upload_glsl_program. */
	VAP (hr->meshes.locs.i_position,  3, GL_FLOAT, position);
	VAP (hr->meshes.locs.i_normal,    3, GL_FLOAT, normal);
	VAP (hr->meshes.locs.i_diffuse,   4, GL_FLOAT, diffuse);
	VAP (hr->meshes.locs.i_ambient,   3, GL_FLOAT, ambient);
	VAP (hr->meshes.locs.i_specular,  4, GL_FLOAT, specular);
	VAP (hr->meshes.locs.i_unknown,   3, GL_FLOAT, unknown);
	VAP (hr->meshes.locs.i_texcoords, 2, GL_FLOAT, texcoords);

	if (hr->debug.flags[HR_DEBUG_NO_INSTANCING]) {
		unsigned i = MIN2 (hr->debug.flags[HR_DEBUG_SELECT_INSTANCE],
		                   mesh->num_instances - 1);
		upload_modelview (hr, mesh, i);
		glDrawArrays (GL_TRIANGLES, 0, mesh->num_tris * 3);
	} else {
		for (i = 0; i < mesh->num_instances; i++) {
			upload_modelview (hr, mesh, i);
			glDrawArrays (GL_TRIANGLES, 0, mesh->num_tris * 3);
		}
	}

	glBindVertexArray (0);
}

#undef OFFSET

static void
upload_vertex_data (hikaru_renderer_t *hr, hikaru_mesh_t *mesh)
{
	VK_ASSERT (mesh);

	mesh->num_tris = hr->push.num_tris;

	/* Generate the VAO if required. */
	if (!hr->meshes.vao) {
		glGenVertexArrays (1, &hr->meshes.vao);
		VK_ASSERT_NO_GL_ERROR ();
	}

	/* Bind the VAO. */
	glBindVertexArray (hr->meshes.vao);
	VK_ASSERT_NO_GL_ERROR ();

	/* Generate the mesh VBO. */
	glGenBuffers (1, &mesh->vbo);

	/* Upload the vertex data to the VBO. */
	glBindBuffer (GL_ARRAY_BUFFER, mesh->vbo);
	glBufferData (GL_ARRAY_BUFFER,
	              sizeof (hikaru_vertex_t) * mesh->num_tris * 3,
	              (const GLvoid *) hr->push.all, GL_DYNAMIC_DRAW);
	VK_ASSERT_NO_GL_ERROR ();
}

void
hikaru_renderer_begin_mesh (vk_renderer_t *rend, uint32_t addr,
                            bool is_static)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;
	hikaru_gpu_t *gpu = hr->gpu;
	unsigned polytype = POLY.type;
	hikaru_mesh_t *mesh;

	VK_ASSERT (hr);
	VK_ASSERT (!hr->meshes.current);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	/* Create a new mesh. */
	mesh = &hr->mesh_list[polytype][hr->num_meshes[polytype]++];
	VK_ASSERT (hr->num_meshes[polytype] < MAX_MESHES);

	/* Make the mesh current and set the rendering state. */
	hr->meshes.current = mesh;
	update_and_set_rendstate (hr, mesh);
	mesh->addr[0] = addr;

	/* Clear the push buffer. */
	hr->push.num_verts = 0;
	hr->push.num_tris = 0;
}

void
hikaru_renderer_end_mesh (vk_renderer_t *rend, uint32_t addr)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) rend;

	VK_ASSERT (hr);

	if (hr->debug.flags[HR_DEBUG_NO_3D])
		return;

	VK_ASSERT (hr->meshes.current);

	/* Upload the pushed vertex data. */
	upload_vertex_data (hr, hr->meshes.current);
	hr->meshes.current->addr[1] = addr;

	/* Make sure there is no current mesh bound. */
	hr->meshes.current = NULL;
}

static void
draw_meshes_for_polytype (hikaru_renderer_t *hr, int polytype)
{
	hikaru_mesh_t *meshes = hr->mesh_list[polytype];
	unsigned num = hr->num_meshes[polytype];
	int j;

	if (hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] >= 0 &&
	    hr->debug.flags[HR_DEBUG_SELECT_POLYTYPE] != polytype)
		goto destroy_meshes;

	LOG (" ==== DRAWING POLYTYPE %d ====", polytype);

	switch (polytype) {
	case HIKARU_POLYTYPE_TRANSPARENT:
	case HIKARU_POLYTYPE_TRANSLUCENT:
		glEnable (GL_BLEND);
		glDepthMask (GL_FALSE);
		break;
	default:
		glDisable (GL_BLEND);
		glDepthMask (GL_TRUE);
		break;
	}
	
	for (j = 0; j < num; j++) {
		hikaru_mesh_t *mesh = &meshes[j];
		draw_mesh (hr, mesh);
	}

destroy_meshes:
	for (j = 0; j < num; j++) {
		hikaru_mesh_t *mesh = &meshes[j];
		if (mesh->vbo)
			glDeleteBuffers (1, &mesh->vbo);
	}
}

static void
draw_scene (hikaru_renderer_t *hr)
{
	static const int sorted_polytypes[] = {
		HIKARU_POLYTYPE_BACKGROUND,
		HIKARU_POLYTYPE_SHADOW_A,
		HIKARU_POLYTYPE_SHADOW_B,
		HIKARU_POLYTYPE_OPAQUE,
		HIKARU_POLYTYPE_TRANSLUCENT,
		HIKARU_POLYTYPE_TRANSPARENT,
	};
	unsigned i;

	glEnable (GL_DEPTH_TEST);
	glDepthFunc (GL_LESS);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	switch (hr->debug.flags[HR_DEBUG_SELECT_CULLFACE]) {
	case -1:
		glDisable (GL_CULL_FACE);
		break;
	case 0:
		glEnable (GL_CULL_FACE);
		glCullFace (GL_BACK);
		break;
	case 1:
		glEnable (GL_CULL_FACE);
		glCullFace (GL_FRONT);
		break;
	}

	for (i = 0; i < NUMELEM (sorted_polytypes); i++)
		draw_meshes_for_polytype (hr, sorted_polytypes[i]);

	glDepthMask (GL_TRUE);
}

/****************************************************************************
 2D
****************************************************************************/

/* TODO implement dirty rectangles. */

static const char *layer_vs_source =
"#version 140\n"
"\n"
"#extension GL_ARB_explicit_attrib_location : require\n"
"\n"
"uniform mat4 u_projection;\n"
"\n"
"layout(location = 0) in vec3 i_position;\n"
"layout(location = 1) in vec2 i_texcoords;\n"
"\n"
"out vec2 p_texcoords;\n"
"\n"
"void main (void) {\n"
"	gl_Position = u_projection * vec4 (i_position, 1.0);\n"
"	p_texcoords = i_texcoords;\n"
"}\n";

static const char *layer_fs_source =
"#version 140\n"
"\n"
"uniform sampler2D u_texture;\n"
"\n"
"in vec2 p_texcoords;\n"
"\n"
"void main (void) {\n"
"	vec4 texel = texture (u_texture, p_texcoords);\n"
"	gl_FragColor = texel;\n"
"}\n";

static const struct {
	vec3f_t position;
	vec2f_t texcoords;
} layer_vbo_data[] = {
	{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
	{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
	{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
	{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
};

#define OFFSET(member_) \
	((const GLvoid *) offsetof (typeof (layer_vbo_data[0]), member_))

static void
build_2d_glsl_state (hikaru_renderer_t *hr)
{
	/* Create the GLSL program. */
	hr->layers.program = compile_program (layer_vs_source, layer_fs_source);
	VK_ASSERT_NO_GL_ERROR ();

	hr->layers.locs.u_projection =
		glGetUniformLocation (hr->layers.program, "u_projection");
	VK_ASSERT (hr->layers.locs.u_projection != (GLuint) -1);

	hr->layers.locs.u_texture =
		glGetUniformLocation (hr->layers.program, "u_texture");
	VK_ASSERT (hr->layers.locs.u_texture != (GLuint) -1);

	/* Create the VAO/VBO. */
	glGenVertexArrays (1, &hr->layers.vao);
	glBindVertexArray (hr->layers.vao);
	VK_ASSERT_NO_GL_ERROR ();

	glGenBuffers (1, &hr->layers.vbo);
	glBindBuffer (GL_ARRAY_BUFFER, hr->layers.vbo);
	glBufferData (GL_ARRAY_BUFFER,
	              sizeof (layer_vbo_data), layer_vbo_data, GL_STATIC_DRAW);
	VK_ASSERT_NO_GL_ERROR ();

	glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE,
	                       sizeof (layer_vbo_data[0]),
	                       OFFSET (position));
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE,
	                       sizeof (layer_vbo_data[0]),
	                       OFFSET (texcoords));
	VK_ASSERT_NO_GL_ERROR ();

	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	VK_ASSERT_NO_GL_ERROR ();

	glBindVertexArray (0);
	VK_ASSERT_NO_GL_ERROR ();
}

#undef OFFSET

static void
destroy_2d_glsl_state (hikaru_renderer_t *hr)
{
	glBindBuffer (GL_ARRAY_BUFFER, 0);
	glDeleteBuffers (1, &hr->layers.vbo);

	glBindVertexArray (0);
	glDeleteVertexArrays (1, &hr->layers.vao);

	destroy_program (hr->layers.program);
}

static void
draw_layer (hikaru_renderer_t *hr, hikaru_layer_t *layer)
{
	mtx4x4f_t projection;
	void *data;
	GLuint id;

	LOG ("drawing LAYER %s", get_layer_str (layer));

	ortho (projection, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f);

	/* Setup the GLSL program. */
	glUseProgram (hr->layers.program);
	glUniformMatrix4fv (hr->layers.locs.u_projection, 1, GL_FALSE,
	                    (const GLfloat *) projection);
	glUniform1i (hr->layers.locs.u_texture, 0);


	/* Upload the layer data to a new texture. */
	glGenTextures (1, &id);
	VK_ASSERT_NO_GL_ERROR ();

	glActiveTexture (GL_TEXTURE0 + 0);
	VK_ASSERT_NO_GL_ERROR ();

	glBindTexture (GL_TEXTURE_2D, id);
	VK_ASSERT_NO_GL_ERROR ();

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	VK_ASSERT_NO_GL_ERROR ();
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	VK_ASSERT_NO_GL_ERROR ();
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	VK_ASSERT_NO_GL_ERROR ();

	data = vk_buffer_get_ptr (hr->gpu->fb,
	                          layer->y0 * 4096 + layer->x0 * 4);

	glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	switch (layer->format) {
	case HIKARU_FORMAT_ABGR1555:
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 2048);

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGB5_A1,
		              640, 480, 0,
		              GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
		              data);
		break;
	case HIKARU_FORMAT_A2BGR10:
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 1024);

		glTexImage2D (GL_TEXTURE_2D, 0,
		              GL_RGB10_A2,
		              640, 480, 0,
		              GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV,
		              data);
		break;
	default:
		VK_ASSERT (0);
	}
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	VK_ASSERT_NO_GL_ERROR ();

	/* Draw. */
	glBindVertexArray (hr->layers.vao);
	VK_ASSERT_NO_GL_ERROR ();

	glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
	VK_ASSERT_NO_GL_ERROR ();

	glBindVertexArray (0);
	glUseProgram (0);
	VK_ASSERT_NO_GL_ERROR ();

	/* Get rid of the layer texture. */
	glDeleteTextures (1, &id);
	VK_ASSERT_NO_GL_ERROR ();
}

static void
draw_layers (hikaru_renderer_t *hr)
{
	hikaru_gpu_t *gpu = hr->gpu;
	hikaru_layer_t *layer;

	if (!LAYERS.enabled)
		return;

	glDisable (GL_DEPTH_TEST);
	VK_ASSERT_NO_GL_ERROR ();

	glEnable (GL_BLEND);
	VK_ASSERT_NO_GL_ERROR ();

	/* Only draw unit 0 for now. I think unit 1 is there only for
	 * multi-monitor, which case we don't care about. */
	layer = &LAYERS.layer[0][1];
	if (layer->enabled && !hr->debug.flags[HR_DEBUG_NO_LAYER2])
		draw_layer (hr, layer);

	layer = &LAYERS.layer[0][0];
	if (layer->enabled && !hr->debug.flags[HR_DEBUG_NO_LAYER2])
		draw_layer (hr, layer);
}

/****************************************************************************
 Interface
****************************************************************************/

static void
hikaru_renderer_begin_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	unsigned i;

	hr->meshes.variant.full = ~0;

	hr->num_vps = 0;
	hr->num_mvs = 0;
	hr->num_instances = 0;
	hr->num_mats = 0;
	hr->num_texs = 0;
	hr->num_lss = 0;

	for (i = 0; i < 8; i++)
		hr->num_meshes[i] = 0;
	hr->total_meshes = 0;

	update_debug_flags (hr);

	/* Note that "the pixel ownership test, the scissor test, dithering,
	 * and the buffer writemasks affect the operation of glClear". */
	glDepthMask (GL_TRUE);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	VK_ASSERT_NO_GL_ERROR ();
}

static void
hikaru_renderer_end_frame (vk_renderer_t *renderer)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;

	VK_ASSERT_NO_GL_ERROR ();

	draw_scene (hr);
	VK_ASSERT_NO_GL_ERROR ();

	draw_layers (hr);
	VK_ASSERT_NO_GL_ERROR ();

	LOG (" ==== RENDSTATE STATISTICS ==== ");
	LOG ("  vp  : %u", hr->num_vps);
	LOG ("  mv  : %u", hr->num_mvs);
	LOG ("  mat : %u", hr->num_mats);
	LOG ("  tex : %u", hr->num_texs);
	LOG ("  ls  : %u", hr->num_lss);
}

static void
hikaru_renderer_reset (vk_renderer_t *renderer)
{
	hikaru_renderer_invalidate_texcache (renderer, NULL);
}

static void
hikaru_renderer_destroy (vk_renderer_t **renderer_)
{
	if (renderer_) {
		hikaru_renderer_t *hr = (hikaru_renderer_t *) *renderer_;
		unsigned i;

		free (hr->vp_list);
		free (hr->mv_list);
		free (hr->mat_list);
		free (hr->tex_list);
		free (hr->ls_list);

		for (i = 0; i < 8; i++)
			free (hr->mesh_list[i]);

		destroy_3d_glsl_state (hr);
		destroy_2d_glsl_state (hr);

		vk_surface_destroy (&hr->textures.debug);
		hikaru_renderer_invalidate_texcache (*renderer_, NULL);
	}
}

#if 0
static vk_surface_t *
build_debug_surface (void)
{
	/* Build a colorful 2x2 checkerboard surface */
	vk_surface_t *surface = vk_surface_new (2, 2, VK_SURFACE_FORMAT_RGBA4444, -1, -1);
	if (!surface)
		return NULL;
	vk_surface_put16 (surface, 0, 0, 0xF00F);
	vk_surface_put16 (surface, 0, 1, 0x0F0F);
	vk_surface_put16 (surface, 1, 0, 0x00FF);
	vk_surface_put16 (surface, 1, 1, 0xFFFF);
	vk_surface_commit (surface);
	return surface;
}
#endif

vk_renderer_t *
hikaru_renderer_new (vk_buffer_t *fb, vk_buffer_t *texram[2])
{
	hikaru_renderer_t *hr;
	int i, ret;

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

	VK_ASSERT_NO_GL_ERROR ();

	hr->vp_list  = (hikaru_viewport_t *) malloc (sizeof (hikaru_viewport_t) * MAX_VIEWPORTS);
	hr->mv_list  = (hikaru_modelview_t *) malloc (sizeof (hikaru_modelview_t) * MAX_MODELVIEWS);
	hr->mat_list = (hikaru_material_t *) malloc (sizeof (hikaru_material_t) * MAX_MATERIALS);
	hr->tex_list = (hikaru_texhead_t *) malloc (sizeof (hikaru_texhead_t) * MAX_TEXHEADS);
	hr->ls_list  = (hikaru_lightset_t *) malloc (sizeof (hikaru_lightset_t) * MAX_LIGHTSETS);
	if (!hr->vp_list || !hr->mv_list ||
	    !hr->mat_list || !hr->tex_list || !hr->ls_list)
		goto fail;

	for (i = 0; i < 8; i++) {
		hr->mesh_list[i] = (hikaru_mesh_t *) malloc (sizeof (hikaru_mesh_t) * MAX_MESHES);
		if (!hr->mesh_list[i])
			goto fail;
	}

	memset ((void *) program_cache, 0, sizeof (program_cache));
	num_programs = 0;

	init_debug_flags (hr);

#if 0
	hr->textures.debug = build_debug_surface ();
	if (!hr->textures.debug)
		goto fail;
#endif

	glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
	VK_ASSERT_NO_GL_ERROR ();

	build_2d_glsl_state (hr);
	VK_ASSERT_NO_GL_ERROR ();

	return (vk_renderer_t *) hr;

fail:
	hikaru_renderer_destroy ((vk_renderer_t **) &hr);
	return NULL;
}

void
hikaru_renderer_set_gpu (vk_renderer_t *renderer, void *gpu_as_void)
{
	hikaru_renderer_t *hr = (hikaru_renderer_t *) renderer;
	hikaru_gpu_t *gpu = (hikaru_gpu_t *) gpu_as_void;

	hr->gpu = gpu;
}
