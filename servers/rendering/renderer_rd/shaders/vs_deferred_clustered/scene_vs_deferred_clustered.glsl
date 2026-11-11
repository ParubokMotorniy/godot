#[vertex]

#version 450

#VERSION_DEFINES

/* Include half precision types. */
#include "../half_inc.glsl"

#include "scene_vs_deferred_clustered_inc.glsl"

#define SHADER_IS_SRGB false
#define SHADER_SPACE_FAR 0.0
#define OUTPUT_IS_MULTIVIEW false

/* INPUT ATTRIBS */

// Always contains vertex position in XYZ, can contain tangent angle in W.
layout(location = 0) in vec4 vertex_angle_attrib;

//only for pure render depth when normal is not used

#if defined(NORMAL_USED) || defined(TANGENT_USED)
// Contains Normal/Axis in RG, can contain tangent in BA.
layout(location = 1) in vec4 axis_tangent_attrib;
#endif

// Location 2 is unused.

#if defined(COLOR_USED)
layout(location = 3) in vec4 color_attrib;
#endif

#ifdef UV_USED
layout(location = 4) in vec2 uv_attrib;
#endif

#if defined(UV2_USED)
layout(location = 5) in vec2 uv2_attrib;
#endif

#if defined(CUSTOM0_USED)
layout(location = 6) in vec4 custom0_attrib;
#endif

#if defined(CUSTOM1_USED)
layout(location = 7) in vec4 custom1_attrib;
#endif

#if defined(CUSTOM2_USED)
layout(location = 8) in vec4 custom2_attrib;
#endif

#if defined(CUSTOM3_USED)
layout(location = 9) in vec4 custom3_attrib;
#endif

#if defined(BONES_USED) || defined(USE_PARTICLE_TRAILS)
layout(location = 10) in uvec4 bone_attrib;
#endif

#if defined(WEIGHTS_USED) || defined(USE_PARTICLE_TRAILS)
layout(location = 11) in vec4 weight_attrib;
#endif

void axis_angle_to_tbn(vec3 axis, float angle, out vec3 tangent, out vec3 binormal, out vec3 normal) {
	float c = cos(angle);
	float s = sin(angle);
	vec3 omc_axis = (1.0 - c) * axis;
	vec3 s_axis = s * axis;
	tangent = omc_axis.xxx * axis + vec3(c, -s_axis.z, s_axis.y);
	binormal = omc_axis.yyy * axis + vec3(s_axis.z, c, -s_axis.x);
	normal = omc_axis.zzz * axis + vec3(-s_axis.y, s_axis.x, c);
}

/* Varyings */

layout(location = 0) out vec3 vertex_interp;

#ifdef NORMAL_USED
layout(location = 1) out vec3 normal_interp;
#endif

#if defined(COLOR_USED)
layout(location = 2) out vec4 color_interp;
#endif

#ifdef UV_USED
layout(location = 3) out vec2 uv_interp;
#endif

#if defined(UV2_USED)
layout(location = 4) out vec2 uv2_interp;
#endif

#ifdef TANGENT_USED
layout(location = 5) out vec3 tangent_interp;
layout(location = 6) out vec3 binormal_interp;
#endif

#ifdef MATERIAL_UNIFORMS_USED
/* clang-format off */
layout(set = MATERIAL_UNIFORM_SET, binding = 0, std140) uniform MaterialUniforms {
#MATERIAL_UNIFORMS
} material;
/* clang-format on */
#endif

float global_time;

#ifdef MODE_DUAL_PARABOLOID

layout(location = 9) out float dp_clip;

#endif

layout(location = 10) out flat uint instance_index_interp;

#define ViewIndex 0
vec2 multiview_uv(vec2 uv) {
	return uv;
}
ivec2 multiview_uv(ivec2 uv) {
	return uv;
}

#if defined(POINT_SIZE_USED) && defined(POINT_COORD_USED)
layout(location = 14) out vec2 point_coord_interp;
#endif

invariant gl_Position;

#GLOBALS

#ifdef USE_DOUBLE_PRECISION
// Helper functions for emulating double precision when adding floats.
vec3 quick_two_sum(vec3 a, vec3 b, out vec3 out_p) {
	vec3 s = a + b;
	out_p = b - (s - a);
	return s;
}

vec3 two_sum(vec3 a, vec3 b, out vec3 out_p) {
	vec3 s = a + b;
	vec3 v = s - a;
	out_p = (a - (s - v)) + (b - v);
	return s;
}

vec3 double_add_vec3(vec3 base_a, vec3 prec_a, vec3 base_b, vec3 prec_b, out vec3 out_precision) {
	vec3 s, t, se, te;
	s = two_sum(base_a, base_b, se);
	t = two_sum(prec_a, prec_b, te);
	se += t;
	s = quick_two_sum(s, se, se);
	se += te;
	s = quick_two_sum(s, se, out_precision);
	return s;
}
#endif

uint multimesh_stride() {
	uint stride = sc_multimesh_format_2d() ? 2 : 3;
	stride += sc_multimesh_has_color() ? 1 : 0;
	stride += sc_multimesh_has_custom_data() ? 1 : 0;
	return stride;
}

void vertex_shader(vec3 vertex_input,
#ifdef NORMAL_USED
		in vec3 normal_input,
#endif
#ifdef TANGENT_USED
		in vec3 tangent_input,
		in vec3 binormal_input,
#endif
		in uint instance_index, in uint multimesh_offset, in SceneData scene_data, in mat3x4 in_model_matrix
#ifdef USE_DOUBLE_PRECISION
		,
		in vec3 model_precision
#endif
) {
	vec4 instance_custom = vec4(0.0);
#if defined(COLOR_USED)
	color_interp = color_attrib;
#endif

	mat4 inv_view_matrix = transpose(mat4(scene_data.inv_view_matrix[0],
			scene_data.inv_view_matrix[1],
			scene_data.inv_view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

	mat4 model_matrix = transpose(mat4(in_model_matrix[0],
			in_model_matrix[1],
			in_model_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef USE_DOUBLE_PRECISION
	vec3 view_precision = scene_data.inv_view_precision.xyz;
#endif

	mat3 model_normal_matrix;
	if (bool(instances.data[instance_index].flags & INSTANCE_FLAGS_NON_UNIFORM_SCALE)) {
		model_normal_matrix = transpose(inverse(mat3(model_matrix)));
	} else {
		model_normal_matrix = mat3(model_matrix);
	}

	mat4 matrix;
	mat4 read_model_matrix = model_matrix;

	if (sc_multimesh()) {
		//multimesh, instances are for it

#ifdef USE_PARTICLE_TRAILS
		uint trail_size = (instances.data[instance_index].flags >> INSTANCE_FLAGS_PARTICLE_TRAIL_SHIFT) & INSTANCE_FLAGS_PARTICLE_TRAIL_MASK;
		uint stride = 3 + 1 + 1; //particles always uses this format

		uint offset = trail_size * stride * INSTANCE_INDEX;

#ifdef COLOR_USED
		vec4 pcolor;
#endif
		{
			uint boffset = offset + bone_attrib.x * stride;
			matrix = mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.x;
#ifdef COLOR_USED
			pcolor = transforms.data[boffset + 3] * weight_attrib.x;
#endif
		}
		if (weight_attrib.y > 0.001) {
			uint boffset = offset + bone_attrib.y * stride;
			matrix += mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.y;
#ifdef COLOR_USED
			pcolor += transforms.data[boffset + 3] * weight_attrib.y;
#endif
		}
		if (weight_attrib.z > 0.001) {
			uint boffset = offset + bone_attrib.z * stride;
			matrix += mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.z;
#ifdef COLOR_USED
			pcolor += transforms.data[boffset + 3] * weight_attrib.z;
#endif
		}
		if (weight_attrib.w > 0.001) {
			uint boffset = offset + bone_attrib.w * stride;
			matrix += mat4(transforms.data[boffset + 0], transforms.data[boffset + 1], transforms.data[boffset + 2], vec4(0.0, 0.0, 0.0, 1.0)) * weight_attrib.w;
#ifdef COLOR_USED
			pcolor += transforms.data[boffset + 3] * weight_attrib.w;
#endif
		}

		instance_custom = transforms.data[offset + 4];

#ifdef COLOR_USED
		color_interp *= pcolor;
#endif

#else
		uint stride = multimesh_stride();
		uint offset = stride * (INSTANCE_INDEX + multimesh_offset);

		if (sc_multimesh_format_2d()) {
			matrix = mat4(transforms.data[offset + 0], transforms.data[offset + 1], vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
			offset += 2;
		} else {
			matrix = mat4(transforms.data[offset + 0], transforms.data[offset + 1], transforms.data[offset + 2], vec4(0.0, 0.0, 0.0, 1.0));
			offset += 3;
		}

		if (sc_multimesh_has_color()) {
#ifdef COLOR_USED
			color_interp *= transforms.data[offset];
#endif
			offset += 1;
		}

		if (sc_multimesh_has_custom_data()) {
			instance_custom = transforms.data[offset];
		}

#endif
		//transpose
		matrix = transpose(matrix);
#if !defined(USE_DOUBLE_PRECISION) || defined(SKIP_TRANSFORM_USED) || defined(VERTEX_WORLD_COORDS_USED) || defined(MODEL_MATRIX_USED)
		// Normally we can bake the multimesh transform into the model matrix, but when using double precision
		// we avoid baking it in so we can emulate high precision.
		read_model_matrix = model_matrix * matrix;
#if !defined(USE_DOUBLE_PRECISION) || defined(SKIP_TRANSFORM_USED) || defined(VERTEX_WORLD_COORDS_USED)
		model_matrix = read_model_matrix;
#endif
#endif
		model_normal_matrix = model_normal_matrix * mat3(matrix);
	}

	vec3 vertex = vertex_input;
#ifdef NORMAL_USED
	vec3 normal_highp = normal_input;
#endif

#ifdef TANGENT_USED
	vec3 tangent = tangent_input;
	vec3 binormal = binormal_input;
#endif

#ifdef UV_USED
	uv_interp = uv_attrib;
#endif

#if defined(UV2_USED)
	uv2_interp = uv2_attrib;
#endif

	vec4 uv_scale = instances.data[instance_index].uv_scale;

	if (uv_scale != vec4(0.0)) { // Compression enabled
#ifdef UV_USED
		uv_interp = (uv_interp - 0.5) * uv_scale.xy;
#endif
#if defined(UV2_USED)
		uv2_interp = (uv2_interp - 0.5) * uv_scale.zw;
#endif
	}

#ifdef OVERRIDE_POSITION
	vec4 position = vec4(1.0);
#endif

	mat4 projection_matrix = scene_data.projection_matrix;
	mat4 inv_projection_matrix = scene_data.inv_projection_matrix;
	vec3 eye_offset = vec3(0.0, 0.0, 0.0);

//using world coordinates
#if !defined(SKIP_TRANSFORM_USED) && defined(VERTEX_WORLD_COORDS_USED)

	vertex = (model_matrix * vec4(vertex, 1.0)).xyz;

#ifdef NORMAL_USED
	normal_highp = model_normal_matrix * normal_highp;
#endif

#ifdef TANGENT_USED

	tangent = model_normal_matrix * tangent;
	binormal = model_normal_matrix * binormal;

#endif
#endif

#ifdef Z_CLIP_SCALE_USED
	float z_clip_scale = 1.0;
#endif

	float roughness_highp = 1.0;

	mat4 read_view_matrix = transpose(mat4(scene_data.view_matrix[0],
			scene_data.view_matrix[1],
			scene_data.view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef USE_DOUBLE_PRECISION
	mat4 modelview = read_view_matrix * model_matrix;

	// We separate the basis from the origin because the basis is fine with single point precision.
	// Then we combine the translations from the model matrix and the view matrix using emulated doubles.
	// We add the result to the vertex and ignore the final lost precision.
	vec3 model_origin = model_matrix[3].xyz;
	if (sc_multimesh()) {
		modelview = modelview * matrix;

		vec3 instance_origin = mat3(model_matrix) * matrix[3].xyz;
		model_origin = double_add_vec3(model_origin, model_precision, instance_origin, vec3(0.0), model_precision);
	}

	// Overwrite the translation part of modelview with improved precision.
	vec3 temp_precision; // Will be ignored.
	modelview[3].xyz = double_add_vec3(model_origin, model_precision, inv_view_matrix[3].xyz, view_precision, temp_precision);
	modelview[3].xyz = mat3(read_view_matrix) * modelview[3].xyz;
#else
	mat4 modelview = read_view_matrix * model_matrix;
#endif
	mat3 modelview_normal = mat3(read_view_matrix) * model_normal_matrix;
	vec2 read_viewport_size = scene_data.viewport_size;

#ifdef POINT_SIZE_USED
	float point_size = 1.0;
#endif

	{
#CODE : VERTEX
	}

	float roughness = roughness_highp;
#ifdef NORMAL_USED
	vec3 normal = normal_highp;
#endif

// using local coordinates (default)
#if !defined(SKIP_TRANSFORM_USED) && !defined(VERTEX_WORLD_COORDS_USED)

	vertex = (modelview * vec4(vertex, 1.0)).xyz;

#ifdef NORMAL_USED
	normal = modelview_normal * normal;
#endif

#ifdef TANGENT_USED

	binormal = modelview_normal * binormal;
	tangent = modelview_normal * tangent;
#endif
#endif // !defined(SKIP_TRANSFORM_USED) && !defined(VERTEX_WORLD_COORDS_USED)

//using world coordinates
#if !defined(SKIP_TRANSFORM_USED) && defined(VERTEX_WORLD_COORDS_USED)

	vertex = (read_view_matrix * vec4(vertex, 1.0)).xyz;
#ifdef NORMAL_USED
	normal = (read_view_matrix * vec4(normal, 0.0)).xyz;
#endif

#ifdef TANGENT_USED
	binormal = (read_view_matrix * vec4(binormal, 0.0)).xyz;
	tangent = (read_view_matrix * vec4(tangent, 0.0)).xyz;
#endif
#endif

	vertex_interp = vertex;

	// Normalize TBN vectors before interpolation, per MikkTSpace.
	// See: http://www.mikktspace.com/
#ifdef NORMAL_USED
	normal_interp = normalize(normal);
#endif

#ifdef TANGENT_USED
	tangent_interp = normalize(tangent);
	binormal_interp = normalize(binormal);
#endif

#ifdef MODE_DUAL_PARABOLOID

	vertex_interp.z *= scene_data.dual_paraboloid_side;

	dp_clip = vertex_interp.z; //this attempts to avoid noise caused by objects sent to the other parabolloid side due to bias

	//for dual paraboloid shadow mapping, this is the fastest but least correct way, as it curves straight edges

	vec3 vtx = vertex_interp;
	float distance = length(vtx);
	vtx = normalize(vtx);
	vtx.xy /= 1.0 - vtx.z;
	vtx.z = (distance / scene_data.z_far);
	vtx.z = vtx.z * 2.0 - 1.0;
	vertex_interp = vtx;

#endif

#ifdef OVERRIDE_POSITION
	gl_Position = position;
#else
	gl_Position = projection_matrix * vec4(vertex_interp, 1.0);
#endif

#ifdef MODE_RENDER_DEPTH
	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_PANCAKE_SHADOWS)) {
		if (gl_Position.z >= 0.9999) {
			gl_Position.z = 0.9999;
		}
	}
#endif

#ifdef Z_CLIP_SCALE_USED
	if (!bool(scene_data.flags & SCENE_DATA_FLAGS_IN_SHADOW_PASS)) {
		gl_Position.z = mix(gl_Position.w, gl_Position.z, z_clip_scale);
	}
#endif

#ifdef POINT_SIZE_USED
	gl_PointSize = point_size;
#endif
}

void _unpack_vertex_attributes(vec4 p_vertex_in, vec3 p_compressed_aabb_position, vec3 p_compressed_aabb_size,
#if defined(NORMAL_USED) || defined(TANGENT_USED)
		vec4 p_normal_in,
#ifdef NORMAL_USED
		out vec3 r_normal,
#endif
		out vec3 r_tangent,
		out vec3 r_binormal,
#endif
		out vec3 r_vertex) {

	r_vertex = p_vertex_in.xyz * p_compressed_aabb_size + p_compressed_aabb_position;
#ifdef NORMAL_USED
	r_normal = oct_to_vec3(p_normal_in.xy * 2.0 - 1.0);
#endif

#if defined(NORMAL_USED) || defined(TANGENT_USED)

	float binormal_sign;

	// This works because the oct value (0, 1) maps onto (0, 0, -1) which encodes to (1, 1).
	// Accordingly, if p_normal_in.z contains octahedral values, it won't equal (0, 1).
	if (p_normal_in.z > 0.0 || p_normal_in.w < 1.0) {
		// Uncompressed format.
		vec2 signed_tangent_attrib = p_normal_in.zw * 2.0 - 1.0;
		r_tangent = oct_to_vec3(vec2(signed_tangent_attrib.x, abs(signed_tangent_attrib.y) * 2.0 - 1.0));
		binormal_sign = sign(signed_tangent_attrib.y);
		r_binormal = normalize(cross(r_normal, r_tangent) * binormal_sign);
	} else {
		// Compressed format.
		float angle = p_vertex_in.w;
		binormal_sign = angle > 0.5 ? 1.0 : -1.0; // 0.5 does not exist in UNORM16, so values are either greater or smaller.
		angle = abs(angle * 2.0 - 1.0) * M_PI; // 0.5 is basically zero, allowing to encode both signs reliably.
		vec3 axis = r_normal;
		axis_angle_to_tbn(axis, angle, r_tangent, r_binormal, r_normal);
		r_binormal *= binormal_sign;
	}
#endif
}

void main() {
	uint instance_index = draw_call.instance_index;
	if (!sc_multimesh()) {
		instance_index += INSTANCE_INDEX;
	}

	instance_index_interp = instance_index;

	vec3 vertex;
#ifdef NORMAL_USED
	vec3 normal;
#endif
#if defined(NORMAL_USED) || defined(TANGENT_USED)
	vec3 tangent;
	vec3 binormal;
#endif

	_unpack_vertex_attributes(
			vertex_angle_attrib,
			instances.data[instance_index].compressed_aabb_position_pad.xyz,
			instances.data[instance_index].compressed_aabb_size_pad.xyz,
#if defined(NORMAL_USED) || defined(TANGENT_USED)
			axis_tangent_attrib,
#ifdef NORMAL_USED
			normal,
#endif
			tangent,
			binormal,
#endif
			vertex);

	global_time = scene_data_block.data.time;
	vertex_shader(vertex,
#ifdef NORMAL_USED
			normal,
#endif
#ifdef TANGENT_USED
			tangent,
			binormal,
#endif
			instance_index, draw_call.multimesh_offset, scene_data_block.data, instances.data[instance_index].transform
#ifdef USE_DOUBLE_PRECISION
			,
			instances.data[instance_index].model_precision.xyz
#endif
	);
}

#[fragment]

#version 450

#VERSION_DEFINES

#define SHADER_IS_SRGB false
#define SHADER_SPACE_FAR 0.0
#define OUTPUT_IS_MULTIVIEW false

/* Include half precision types. */
#include "../half_inc.glsl"

#include "scene_vs_deferred_clustered_inc.glsl"

/* Varyings */

layout(location = 0) in vec3 vertex_interp;

#ifdef NORMAL_USED
layout(location = 1) in vec3 normal_interp;
#endif

#if defined(COLOR_USED)
layout(location = 2) in vec4 color_interp;
#endif

#ifdef UV_USED
layout(location = 3) in vec2 uv_interp;
#endif

#if defined(UV2_USED)
layout(location = 4) in vec2 uv2_interp;
#endif

#ifdef TANGENT_USED
layout(location = 5) in vec3 tangent_interp;
layout(location = 6) in vec3 binormal_interp;
#endif

#ifdef MODE_DUAL_PARABOLOID

layout(location = 9) in float dp_clip;

#endif

layout(location = 10) in flat uint instance_index_interp;

#define ViewIndex 0
vec2 multiview_uv(vec2 uv) {
	return uv;
}
ivec2 multiview_uv(ivec2 uv) {
	return uv;
}

#if defined(POINT_SIZE_USED) && defined(POINT_COORD_USED)
layout(location = 14) in vec2 point_coord_interp;
#endif

//defines to keep compatibility with vertex

#define projection_matrix scene_data.projection_matrix
#define inv_projection_matrix scene_data.inv_projection_matrix

#define global_time scene_data_block.data.time

#ifdef MATERIAL_UNIFORMS_USED
/* clang-format off */
layout(set = MATERIAL_UNIFORM_SET, binding = 0, std140) uniform MaterialUniforms {
#MATERIAL_UNIFORMS
} material;
/* clang-format on */
#endif

#GLOBALS

#ifdef MODE_RENDER_DEPTH

#ifdef MODE_RENDER_NORMAL_ROUGHNESS
layout(location = 0) out vec4 normal_roughness_output_buffer;
#endif

#else // RENDER DEPTH

layout(location = 0) out vec4 frag_color;

#endif // RENDER DEPTH

#include "../scene_forward_aa_inc.glsl"

#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

// Default to SPECULAR_SCHLICK_GGX.
#if !defined(SPECULAR_DISABLED) && !defined(SPECULAR_SCHLICK_GGX) && !defined(SPECULAR_TOON)
#define SPECULAR_SCHLICK_GGX
#endif

#include "../scene_forward_lights_inc.glsl"

#endif //!defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

#ifndef MODE_RENDER_DEPTH

void cluster_get_item_range(uint p_offset, out uint item_min, out uint item_max, out uint item_from, out uint item_to) {
	uint item_min_max = cluster_buffer.data[p_offset];
	item_min = item_min_max & 0xFFFFu;
	item_max = item_min_max >> 16;

	item_from = item_min >> 5;
	item_to = (item_max == 0) ? 0 : ((item_max - 1) >> 5) + 1; //side effect of how it is stored, as item_max 0 means no elements
}

uint cluster_get_range_clip_mask(uint i, uint z_min, uint z_max) {
	int local_min = clamp(int(z_min) - int(i) * 32, 0, 31);
	int mask_width = min(int(z_max) - int(z_min), 32 - local_min);
	return bitfieldInsert(uint(0), uint(0xFFFFFFFF), local_min, mask_width);
}

#endif //!MODE_RENDER DEPTH

#ifdef MODE_RENDER_NORMAL_ROUGHNESS
// https://advances.realtimerendering.com/s2010/Kaplanyan-CryEngine3(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf
vec3 encode24(vec3 v) {
	// Unsigned normal (handles most symmetry)
	vec3 vNormalUns = abs(v);
	// Get the major axis for our collapsed cubemap lookup
	float maxNAbs = max(vNormalUns.z, max(vNormalUns.x, vNormalUns.y));
	// Get the collapsed cubemap texture coordinates
	vec2 vTexCoord = vNormalUns.z < maxNAbs ? (vNormalUns.y < maxNAbs ? vNormalUns.yz : vNormalUns.xz) : vNormalUns.xy;
	vTexCoord /= maxNAbs;
	vTexCoord = vTexCoord.x < vTexCoord.y ? vTexCoord.yx : vTexCoord.xy;
	// Stretch:
	vTexCoord.y /= vTexCoord.x;
	float fFittingScale = texture(sampler2D(best_fit_normal_texture, SAMPLER_NEAREST_CLAMP), vTexCoord).r;
	// Make vector touch unit cube
	vec3 result = v / maxNAbs;
	// scale the normal to get the best fit
	result *= fFittingScale;
	return result;
}
#endif // MODE_RENDER_NORMAL_ROUGHNESS

void fragment_shader(in SceneData scene_data) {
	uint instance_index = instance_index_interp;

#ifdef PREMUL_ALPHA_USED
	float premul_alpha = 1.0;
#endif // PREMUL_ALPHA_USED
	//lay out everything, whatever is unused is optimized away anyway
	vec3 vertex = vertex_interp;
	vec3 eye_offset = vec3(0.0, 0.0, 0.0);
	vec3 view_highp = -normalize(vertex_interp);
	vec3 albedo_highp = vec3(1.0);
	vec3 backlight = vec3(0.0);
	// Not supported by this renderer (no SSS/transmittance), kept so user shaders writing them still compile.
	vec4 transmittance_color = vec4(0.0, 0.0, 0.0, 1.0);
	float transmittance_depth = 0.0;
	float transmittance_boost = 0.0;
	float sss_strength = 0.0;
	vec4 fog = vec4(0.0, 0.0, 0.0, 1.0); // Fog is not supported by this renderer.
	float metallic_highp = 0.0;
	float specular = 0.5;
	vec3 emission = vec3(0.0);
	float roughness_highp = 1.0;
	float rim = 0.0;
	float rim_tint = 0.0;
	float clearcoat = 0.0;
	float clearcoat_roughness = 0.0;
	float anisotropy = 0.0;
	vec2 anisotropy_flow = vec2(1.0, 0.0);
	vec3 energy_compensation = vec3(1.0);
#if defined(CUSTOM_RADIANCE_USED)
	vec4 custom_radiance = vec4(0.0);
#endif
#if defined(CUSTOM_IRRADIANCE_USED)
	vec4 custom_irradiance = vec4(0.0);
#endif

	float ao = 1.0;
	float ao_light_affect = 0.0;

	float alpha_highp = float(instances.data[instance_index].flags >> INSTANCE_FLAGS_FADE_SHIFT) / float(255.0);

#ifdef TANGENT_USED
	vec3 binormal = binormal_interp;
	vec3 tangent = tangent_interp;
#else
	vec3 binormal = vec3(0.0);
	vec3 tangent = vec3(0.0);
#endif

#ifdef NORMAL_USED
	vec3 normal_highp = normal_interp;
#if defined(DO_SIDE_CHECK)
	if (!gl_FrontFacing) {
		normal_highp = -normal_highp;
	}
#endif // DO_SIDE_CHECK
#endif // NORMAL_USED

#ifdef UV_USED
	vec2 uv = uv_interp;
#endif

#if defined(UV2_USED)
	vec2 uv2 = uv2_interp;
#endif

#if defined(COLOR_USED)
	vec4 color = color_interp;
#endif

#if defined(NORMAL_MAP_USED)

	vec3 normal_map = vec3(0.5);
#endif

#if defined(BENT_NORMAL_MAP_USED)
	vec3 bent_normal_vector = vec3(0.5);
	vec3 bent_normal_map = vec3(0.5);
#endif

	float normal_map_depth = 1.0;

	vec2 screen_uv = gl_FragCoord.xy * scene_data.screen_pixel_size;

#ifdef ALPHA_SCISSOR_USED
	float alpha_scissor_threshold = 1.0;
#endif // ALPHA_SCISSOR_USED

#ifdef ALPHA_HASH_USED
	float alpha_hash_scale = 1.0;
#endif // ALPHA_HASH_USED

#ifdef ALPHA_ANTIALIASING_EDGE_USED
	float alpha_antialiasing_edge = 0.0;
	vec2 alpha_texture_coordinate = vec2(0.0, 0.0);
#endif // ALPHA_ANTIALIASING_EDGE_USED

	mat4 inv_view_matrix = transpose(mat4(scene_data.inv_view_matrix[0],
			scene_data.inv_view_matrix[1],
			scene_data.inv_view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));
	mat4 read_model_matrix = transpose(mat4(instances.data[instance_index].transform[0],
			instances.data[instance_index].transform[1],
			instances.data[instance_index].transform[2],
			vec4(0.0, 0.0, 0.0, 1.0)));

#ifdef LIGHT_VERTEX_USED
	vec3 light_vertex = vertex;
#endif //LIGHT_VERTEX_USED

	mat3 model_normal_matrix;
	if (bool(instances.data[instance_index].flags & INSTANCE_FLAGS_NON_UNIFORM_SCALE)) {
		model_normal_matrix = transpose(inverse(mat3(read_model_matrix)));
	} else {
		model_normal_matrix = mat3(read_model_matrix);
	}

	mat4 read_view_matrix = transpose(mat4(scene_data.view_matrix[0],
			scene_data.view_matrix[1],
			scene_data.view_matrix[2],
			vec4(0.0, 0.0, 0.0, 1.0)));
	vec2 read_viewport_size = scene_data.viewport_size;

#ifdef POINT_COORD_USED
#ifdef POINT_SIZE_USED
	vec2 point_coord = gl_PointCoord;
#else // !POINT_SIZE_USED
	vec2 point_coord = vec2(0.5);
#endif
#endif

	{
#CODE : FRAGMENT
	}

	float roughness = roughness_highp;
	float metallic = metallic_highp;
	vec3 albedo = albedo_highp;
	float alpha = alpha_highp;
#ifdef NORMAL_USED
	vec3 normal = normal_highp;
#endif

#ifdef LIGHT_VERTEX_USED
	vertex = light_vertex;
	vec3 view = -normalize(vertex);
#else
	vec3 view = view_highp;
#endif //LIGHT_VERTEX_USED

#ifdef NORMAL_USED
	vec3 geo_normal = normalize(normal);
#endif // NORMAL_USED

#ifndef USE_SHADOW_TO_OPACITY

#ifdef ALPHA_SCISSOR_USED
	if (alpha < alpha_scissor_threshold) {
		discard;
	}
#endif // ALPHA_SCISSOR_USED

// alpha hash can be used in unison with alpha antialiasing
#ifdef ALPHA_HASH_USED
	vec3 object_pos = (inverse(read_model_matrix) * inv_view_matrix * vec4(vertex, 1.0)).xyz;
	if (alpha < compute_alpha_hash_threshold(object_pos, alpha_hash_scale)) {
		discard;
	}
#endif // ALPHA_HASH_USED

// If we are not edge antialiasing, we need to remove the output alpha channel from scissor and hash
#if (defined(ALPHA_SCISSOR_USED) || defined(ALPHA_HASH_USED)) && !defined(ALPHA_ANTIALIASING_EDGE_USED)
	alpha = 1.0;
#endif

#ifdef ALPHA_ANTIALIASING_EDGE_USED
// If alpha scissor is used, we must further the edge threshold, otherwise we won't get any edge feather
#ifdef ALPHA_SCISSOR_USED
	alpha_antialiasing_edge = clamp(alpha_scissor_threshold + alpha_antialiasing_edge, 0.0, 1.0);
#endif
	alpha = compute_alpha_antialiasing_edge(alpha, alpha_texture_coordinate, alpha_antialiasing_edge);
#endif // ALPHA_ANTIALIASING_EDGE_USED

#ifdef MODE_RENDER_DEPTH
#if defined(USE_OPAQUE_PREPASS) || defined(ALPHA_ANTIALIASING_EDGE_USED)
	if (alpha < scene_data.opaque_prepass_threshold) {
		discard;
	}
#endif // USE_OPAQUE_PREPASS || ALPHA_ANTIALIASING_EDGE_USED
#endif // MODE_RENDER_DEPTH

#endif // !USE_SHADOW_TO_OPACITY

#if defined(NORMAL_MAP_USED)
	normal_map.xy = normal_map.xy * 2.0 - 1.0;
	normal_map.z = sqrt(max(0.0, 1.0 - dot(normal_map.xy, normal_map.xy))); //always ignore Z, as it can be RG packed, Z may be pos/neg, etc.

	// Tangent-space transformation is performed using unnormalized TBN vectors, per MikkTSpace.
	// See: http://www.mikktspace.com/
	normal = normalize(mix(normal, tangent * normal_map.x + binormal * normal_map.y + normal * normal_map.z, normal_map_depth));
#elif defined(NORMAL_USED)
	normal = geo_normal;
#endif // NORMAL_MAP_USED

#ifdef BENT_NORMAL_MAP_USED
	bent_normal_map.xy = bent_normal_map.xy * 2.0 - 1.0;
	bent_normal_map.z = sqrt(max(0.0, 1.0 - dot(bent_normal_map.xy, bent_normal_map.xy)));

	bent_normal_vector = normalize(tangent * bent_normal_map.x + binormal * bent_normal_map.y + normal * bent_normal_map.z);
#endif

#ifdef LIGHT_ANISOTROPY_USED

	if (anisotropy > 0.01) {
		mat3 rot = mat3(normalize(tangent), normalize(binormal), normal);
		// Make local to space.
		tangent = normalize(rot * vec3(anisotropy_flow.x, anisotropy_flow.y, 0.0));
		binormal = normalize(rot * vec3(-anisotropy_flow.y, anisotropy_flow.x, 0.0));
	}

#endif

#ifndef MODE_RENDER_DEPTH
	uvec2 cluster_pos = uvec2(gl_FragCoord.xy) >> implementation_data.cluster_shift;
	uint cluster_offset = (implementation_data.cluster_width * cluster_pos.y + cluster_pos.x) * (implementation_data.max_cluster_element_count_div_32 + 32);

	uint cluster_z = uint(clamp((-vertex.z / scene_data.z_far) * 32.0, 0.0, 31.0));

	//used for interpolating anything cluster related
	vec3 vertex_ddx = dFdx(vertex);
	vec3 vertex_ddy = dFdy(vertex);
#endif //not render depth

	/////////////////////// LIGHTING //////////////////////////////

#ifdef NORMAL_USED
	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_ROUGHNESS_LIMITER)) {
		//https://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
		float roughness2 = roughness * roughness;
		vec3 dndu = dFdx(normal), dndv = dFdy(normal);
		float variance = scene_data.roughness_limiter_amount * (dot(dndu, dndu) + dot(dndv, dndv));
		float kernelRoughness2 = min(2.0 * variance, scene_data.roughness_limiter_limit); //limit effect
		float filteredRoughness2 = min(1.0, roughness2 + kernelRoughness2);
		roughness = sqrt(filteredRoughness2);

		// Reject very small roughness values. Lack of precision can collapse
		// roughness^4 to 0 in GGX specular equations and cause divisions by zero.
		if (roughness < 0.00000001) {
			roughness = 0.0;
		}
	}
#endif
	//apply energy conservation

	vec3 direct_specular_light = vec3(0.0, 0.0, 0.0);
	vec3 indirect_specular_light = vec3(0.0, 0.0, 0.0);
	vec3 diffuse_light = vec3(0.0, 0.0, 0.0);
	vec3 ambient_light = vec3(0.0, 0.0, 0.0);
#ifndef MODE_UNSHADED
	emission *= scene_data.emissive_exposure_normalization;
#endif

	/////////////////////// AMBIENT / ENVIRONMENT (sky radiance) //////////////////////////////

#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

#ifndef AMBIENT_LIGHT_DISABLED
// Use bent normal for indirect lighting where possible
#ifdef BENT_NORMAL_MAP_USED
	vec3 indirect_normal = bent_normal_vector;
#else
	vec3 indirect_normal = normal;
#endif

	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_REFLECTION_CUBEMAP)) {
#ifdef LIGHT_ANISOTROPY_USED
		// https://google.github.io/filament/Filament.html#lighting/imagebasedlights/anisotropy
		vec3 anisotropic_direction = anisotropy >= 0.0 ? binormal : tangent;
		vec3 anisotropic_tangent = cross(anisotropic_direction, view);
		vec3 anisotropic_normal = cross(anisotropic_tangent, anisotropic_direction);
		vec3 bent_normal = normalize(mix(normal, anisotropic_normal, abs(anisotropy) * clamp(5.0 * roughness, 0.0, 1.0)));
		vec3 ref_vec = reflect(-view, bent_normal);
		ref_vec = mix(ref_vec, bent_normal, roughness * roughness);
#else
		vec3 ref_vec = reflect(-view, normal);
		ref_vec = mix(ref_vec, normal, roughness * roughness);
#endif

		float horizon = min(1.0 + dot(ref_vec, normal), 1.0);
		ref_vec = scene_data.radiance_inverse_xform * ref_vec;

#ifdef USE_RADIANCE_OCTMAP_ARRAY

		float roughness_lod, blend;

		blend = modf(sqrt(roughness) * MAX_ROUGHNESS_LOD, roughness_lod);

		float ref_lod = vec3_to_oct_lod(dFdx(ref_vec), dFdy(ref_vec), scene_data_block.data.radiance_pixel_size);
		vec2 ref_uv = vec3_to_oct_with_border(ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		vec3 indirect_sample_a = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, roughness_lod), ref_lod).rgb;
		vec3 indirect_sample_b = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, roughness_lod + 1), ref_lod).rgb;
		indirect_specular_light = mix(indirect_sample_a, indirect_sample_b, blend);

#else
		float roughness_lod = sqrt(roughness) * MAX_ROUGHNESS_LOD;
		vec2 ref_uv = vec3_to_oct_with_border(ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		indirect_specular_light = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), ref_uv, roughness_lod).rgb;

#endif //USE_RADIANCE_OCTMAP_ARRAY
		indirect_specular_light *= scene_data.IBL_exposure_normalization;
		indirect_specular_light *= horizon * horizon;
		indirect_specular_light *= scene_data.ambient_light_color_energy.a;
	}

#if defined(CUSTOM_RADIANCE_USED)
	indirect_specular_light = mix(indirect_specular_light, custom_radiance.rgb, custom_radiance.a);
#endif

	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_AMBIENT_LIGHT)) {
		ambient_light = scene_data.ambient_light_color_energy.rgb;

		if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_AMBIENT_CUBEMAP)) {
			vec3 ambient_dir = scene_data.radiance_inverse_xform * indirect_normal;
#ifdef USE_RADIANCE_OCTMAP_ARRAY
			float ambient_lod = vec3_to_oct_lod(dFdx(ambient_dir), dFdy(ambient_dir), scene_data_block.data.radiance_pixel_size);
			vec2 ambient_uv = vec3_to_oct_with_border(ambient_dir, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
			vec3 cubemap_ambient = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ambient_uv, MAX_ROUGHNESS_LOD), ambient_lod).rgb;
#else
			float roughness_lod = MAX_ROUGHNESS_LOD;
			vec2 ambient_uv = vec3_to_oct_with_border(ambient_dir, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
			vec3 cubemap_ambient = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), ambient_uv, roughness_lod).rgb;
#endif //USE_RADIANCE_OCTMAP_ARRAY
			cubemap_ambient *= scene_data.IBL_exposure_normalization;
			ambient_light = mix(ambient_light, cubemap_ambient * scene_data.ambient_light_color_energy.a, scene_data.ambient_color_sky_mix);
		}
	}
#if defined(CUSTOM_IRRADIANCE_USED)
	ambient_light = mix(ambient_light, custom_irradiance.rgb, custom_irradiance.a);
#endif

#ifdef LIGHT_CLEARCOAT_USED
	vec3 cc_specular_light = vec3(0.0);
	vec3 cc_ref_vec = vec3(0.0);

	if (bool(scene_data.flags & SCENE_DATA_FLAGS_USE_REFLECTION_CUBEMAP)) {
		cc_ref_vec = reflect(-view, geo_normal);
		cc_ref_vec = mix(cc_ref_vec, geo_normal, mix(0.001, 0.1, clearcoat_roughness));

		vec3 cc_radiance_ref_vec = scene_data.radiance_inverse_xform * cc_ref_vec;
		float roughness_lod = sqrt(mix(0.001, 0.1, clearcoat_roughness)) * MAX_ROUGHNESS_LOD;
#ifdef USE_RADIANCE_OCTMAP_ARRAY

		float lod, blend;
		blend = modf(roughness_lod, lod);

		float ref_lod = vec3_to_oct_lod(dFdx(cc_radiance_ref_vec), dFdy(cc_radiance_ref_vec), scene_data_block.data.radiance_pixel_size);
		vec2 ref_uv = vec3_to_oct_with_border(cc_radiance_ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		vec3 clearcoat_sample_a = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, lod), ref_lod).rgb;
		vec3 clearcoat_sample_b = textureLod(sampler2DArray(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), vec3(ref_uv, lod + 1), ref_lod).rgb;
		vec3 clearcoat_light = mix(clearcoat_sample_a, clearcoat_sample_b, blend);

#else
		vec2 ref_uv = vec3_to_oct_with_border(cc_radiance_ref_vec, vec2(scene_data_block.data.radiance_border_size, 1.0 - scene_data_block.data.radiance_border_size * 2.0));
		vec3 clearcoat_light = textureLod(sampler2D(radiance_octmap, DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP), ref_uv, roughness_lod).rgb;

#endif //USE_RADIANCE_OCTMAP_ARRAY
		cc_specular_light += clearcoat_light * scene_data.IBL_exposure_normalization * scene_data.ambient_light_color_energy.a;
	}
#endif // LIGHT_CLEARCOAT_USED

	// SSAO (computed by a compute pass after the depth pre-pass).
	if (bool(implementation_data.ss_effects_flags & SCREEN_SPACE_EFFECTS_FLAGS_USE_SSAO)) {
		float ssao = texture(sampler2D(ao_buffer, SAMPLER_LINEAR_CLAMP), screen_uv).r;
		ao = min(ao, ssao);
		ao_light_affect = mix(ao_light_affect, max(ao_light_affect, implementation_data.ssao_light_affect), implementation_data.ssao_ao_affect);
	}

	//finalize ambient light here
	{
		ambient_light *= ao;
#ifndef SPECULAR_OCCLUSION_DISABLED
#ifdef BENT_NORMAL_MAP_USED
		// Apply cone to cone intersection with cosine weighted assumption:
		// https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf
		float cos_a_v = sqrt(1.0 - ao);
		float limited_roughness = max(roughness, 0.01); // Avoid artifacts at really low roughness.
		float cos_a_s = exp2((-log(10.0) / log(2.0)) * limited_roughness * limited_roughness);
		float cos_b = dot(bent_normal_vector, reflect(-view, normal));

		// Intersection between the spherical caps of the visibility and specular cone.
		// Based on Christopher Oat and Pedro V. Sander's "Ambient aperture lighting":
		// https://advances.realtimerendering.com/s2006/Chapter8-Ambient_Aperture_Lighting.pdf
		float r1 = acos(cos_a_v);
		float r2 = acos(cos_a_s);
		float d = acos(cos_b);
		float area = 0.0;

		if (d <= max(r1, r2) - min(r1, r2)) {
			// One cap is enclosed in the other.
			area = M_TAU - M_TAU * max(cos_a_v, cos_a_s);
		} else if (d >= r1 + r2) {
			// No intersection.
			area = 0.0;
		} else {
			float delta = abs(r1 - r2);
			float x = 1.0 - clamp((d - delta) / (r1 + r2 - delta), 0.0, 1.0);
			area = smoothstep(0.0, 1.0, x);
			area *= M_TAU - M_TAU * max(cos_a_v, cos_a_s);
		}

		float specular_occlusion = area / (M_TAU * (1.0 - cos_a_s));
		indirect_specular_light *= specular_occlusion;
#else // BENT_NORMAL_MAP_USED
		float specular_occlusion = (ambient_light.r * 0.3 + ambient_light.g * 0.59 + ambient_light.b * 0.11) * 2.0; // Luminance of ambient light.
		specular_occlusion = min(specular_occlusion * 4.0, 1.0); // This multiplication preserves speculars on bright areas.

		float reflective_f = (1.0 - roughness) * metallic;
		// 10.0 is a magic number, it gives the intended effect in most scenarios.
		// Low enough for occlusion, high enough for reaction to lights and shadows.
		specular_occlusion = max(min(reflective_f * specular_occlusion * 10.0, 1.0), specular_occlusion);
		indirect_specular_light *= specular_occlusion;
#endif // BENT_NORMAL_MAP_USED
#endif // SPECULAR_OCCLUSION_DISABLED
		ambient_light *= albedo.rgb;
	}
#endif // AMBIENT_LIGHT_DISABLED

	// convert ao to direct light ao
	ao = mix(1.0, ao, ao_light_affect);

	//this saves some VGPRs
	vec3 f0 = F0(metallic, specular, albedo);

#ifdef LIGHT_CLEARCOAT_USED
	// The base layer's f0 is computed assuming an interface from air to an IOR
	// of 1.5, but the clear coat layer forms an interface from IOR 1.5 to IOR
	// 1.5. We recompute f0 by first computing its IOR, then reconverting to f0
	// by using the correct interface
	f0 = mix(f0, f0_Clear_Coat_To_Surface(f0), clearcoat);
#endif

#ifndef AMBIENT_LIGHT_DISABLED
	{
#if defined(DIFFUSE_TOON)
		//simplify for toon, as
		indirect_specular_light *= specular * metallic * albedo * 2.0;
#else
		// Base Layer
		float NdotV = clamp(dot(normal, view), 0.0001, 1.0);
		vec2 envBRDF = prefiltered_dfg(roughness, NdotV).xy;
		// Multiscattering
		energy_compensation = get_energy_compensation(f0, envBRDF.y);

		// cheap luminance approximation
		float f90 = clamp(50.0 * f0.g, metallic, 1.0);
		indirect_specular_light *= energy_compensation * ((f90 - f0) * envBRDF.x + f0 * envBRDF.y);

#ifdef LIGHT_CLEARCOAT_USED
		float geo_NdotV = max(dot(geo_normal, view), 0.0001); // We want to use geometric normal, not normal_map
		// The clearcoat layer assumes an IOR of 1.5 (4% reflectance).
		// Attenuate underlying diffuse/specular by clearcoat fresnel (ONLY fresnel, hence we don't just invert the BRDF below).
		float NdotV5 = SchlickFresnel(geo_NdotV);
		float F = mix(0.04, 1.0, NdotV5) * clearcoat;
		float cc_attenuation = 1.0 - F;

		ambient_light *= cc_attenuation;
		indirect_specular_light *= cc_attenuation;

		// Clearcoat Layer
		// We don't need DFG for clearcoat, so we can use the fresnel directly.
		indirect_specular_light += cc_specular_light * F;
#endif

#endif // DIFFUSE_TOON
	}

#endif // !AMBIENT_LIGHT_DISABLED
#endif //!defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

	/////////////////////// DIRECT LIGHTS (directional + clustered omni/spot/area) //////////////////////////////

#if !defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

	{ // Directional light.

		// Do shadow and lighting in two passes to reduce register pressure.
#ifndef SHADOWS_DISABLED
		uint shadow0 = 0;
		uint shadow1 = 0;

		for (uint i = 0; i < 8; i++) {
			if (i >= scene_data.directional_light_count) {
				break;
			}

			if (!bool(directional_lights.data[i].mask & instances.data[instance_index].layer_mask)) {
				continue; //not masked
			}

			float shadow = 1.0;

			if (directional_lights.data[i].shadow_opacity > 0.001) {
				float depth_z = -vertex.z;
				vec3 light_dir = directional_lights.data[i].direction;
				vec3 base_normal_bias = geo_normal * (1.0 - max(0.0, dot(light_dir, -geo_normal)));

#define BIAS_FUNC(m_var, m_idx)                                                                 \
	m_var.xyz += light_dir * directional_lights.data[i].shadow_bias[m_idx];                     \
	vec3 normal_bias = base_normal_bias * directional_lights.data[i].shadow_normal_bias[m_idx]; \
	normal_bias -= light_dir * dot(light_dir, normal_bias);                                     \
	m_var.xyz += normal_bias;

				//version with soft shadows, more expensive
				if (sc_use_directional_soft_shadows() && directional_lights.data[i].softshadow_angle > 0) {
					uint blend_count = 0;
					const uint blend_max = directional_lights.data[i].blend_splits ? 2 : 1;

					if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 0)

						vec4 pssm_coord = (directional_lights.data[i].shadow_matrix1 * v);
						pssm_coord /= pssm_coord.w;

						float range_pos = dot(directional_lights.data[i].direction, v.xyz);
						float range_begin = directional_lights.data[i].shadow_range_begin.x;
						float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
						vec2 tex_scale = directional_lights.data[i].uv_scale1 * test_radius;
						shadow = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count);
						blend_count++;
					}

					if (blend_count < blend_max && depth_z < directional_lights.data[i].shadow_split_offsets.y) {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 1)

						vec4 pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
						pssm_coord /= pssm_coord.w;

						float range_pos = dot(directional_lights.data[i].direction, v.xyz);
						float range_begin = directional_lights.data[i].shadow_range_begin.y;
						float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
						vec2 tex_scale = directional_lights.data[i].uv_scale2 * test_radius;
						float s = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count);

						if (blend_count == 0) {
							shadow = s;
						} else {
							//blend
							float blend = smoothstep(0.0, directional_lights.data[i].shadow_split_offsets.x, depth_z);
							shadow = mix(shadow, s, blend);
						}

						blend_count++;
					}

					if (blend_count < blend_max && depth_z < directional_lights.data[i].shadow_split_offsets.z) {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 2)

						vec4 pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
						pssm_coord /= pssm_coord.w;

						float range_pos = dot(directional_lights.data[i].direction, v.xyz);
						float range_begin = directional_lights.data[i].shadow_range_begin.z;
						float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
						vec2 tex_scale = directional_lights.data[i].uv_scale3 * test_radius;
						float s = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count);

						if (blend_count == 0) {
							shadow = s;
						} else {
							//blend
							float blend = smoothstep(directional_lights.data[i].shadow_split_offsets.x, directional_lights.data[i].shadow_split_offsets.y, depth_z);
							shadow = mix(shadow, s, blend);
						}

						blend_count++;
					}

					if (blend_count < blend_max) {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 3)

						vec4 pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
						pssm_coord /= pssm_coord.w;

						float range_pos = dot(directional_lights.data[i].direction, v.xyz);
						float range_begin = directional_lights.data[i].shadow_range_begin.w;
						float test_radius = (range_pos - range_begin) * directional_lights.data[i].softshadow_angle;
						vec2 tex_scale = directional_lights.data[i].uv_scale4 * test_radius;
						float s = sample_directional_soft_shadow(directional_shadow_atlas, pssm_coord.xyz, tex_scale * directional_lights.data[i].soft_shadow_scale, scene_data.taa_frame_count);

						if (blend_count == 0) {
							shadow = s;
						} else {
							//blend
							float blend = smoothstep(directional_lights.data[i].shadow_split_offsets.y, directional_lights.data[i].shadow_split_offsets.z, depth_z);
							shadow = mix(shadow, s, blend);
						}
					}

				} else { //no soft shadows

					vec4 pssm_coord;
					float blur_factor;

					if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 0)

						pssm_coord = (directional_lights.data[i].shadow_matrix1 * v);
						blur_factor = 1.0;
					} else if (depth_z < directional_lights.data[i].shadow_split_offsets.y) {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 1)

						pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
						// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
						blur_factor = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.y;
					} else if (depth_z < directional_lights.data[i].shadow_split_offsets.z) {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 2)

						pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
						// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
						blur_factor = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.z;
					} else {
						vec4 v = vec4(vertex, 1.0);

						BIAS_FUNC(v, 3)

						pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
						// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
						blur_factor = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.w;
					}

					pssm_coord /= pssm_coord.w;

					shadow = sample_directional_pcf_shadow(directional_shadow_atlas, scene_data.directional_shadow_pixel_size * directional_lights.data[i].soft_shadow_scale * (blur_factor + (1.0 - blur_factor) * float(directional_lights.data[i].blend_splits)), pssm_coord, scene_data.taa_frame_count);

					if (directional_lights.data[i].blend_splits) {
						float pssm_blend;
						float blur_factor2;

						if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
							vec4 v = vec4(vertex, 1.0);
							BIAS_FUNC(v, 1)
							pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
							pssm_blend = smoothstep(directional_lights.data[i].shadow_split_offsets.x - directional_lights.data[i].shadow_split_offsets.x * 0.1, directional_lights.data[i].shadow_split_offsets.x, depth_z);
							// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
							blur_factor2 = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.y;
						} else if (depth_z < directional_lights.data[i].shadow_split_offsets.y) {
							vec4 v = vec4(vertex, 1.0);
							BIAS_FUNC(v, 2)
							pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
							pssm_blend = smoothstep(directional_lights.data[i].shadow_split_offsets.y - directional_lights.data[i].shadow_split_offsets.y * 0.1, directional_lights.data[i].shadow_split_offsets.y, depth_z);
							// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
							blur_factor2 = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.z;
						} else if (depth_z < directional_lights.data[i].shadow_split_offsets.z) {
							vec4 v = vec4(vertex, 1.0);
							BIAS_FUNC(v, 3)
							pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
							pssm_blend = smoothstep(directional_lights.data[i].shadow_split_offsets.z - directional_lights.data[i].shadow_split_offsets.z * 0.1, directional_lights.data[i].shadow_split_offsets.z, depth_z);
							// Adjust shadow blur with reference to the first split to reduce discrepancy between shadow splits.
							blur_factor2 = directional_lights.data[i].shadow_split_offsets.x / directional_lights.data[i].shadow_split_offsets.w;
						} else {
							pssm_blend = 0.0; //if no blend, same coord will be used (divide by z will result in same value, and already cached)
							blur_factor2 = 1.0;
						}

						pssm_coord /= pssm_coord.w;

						float shadow2 = sample_directional_pcf_shadow(directional_shadow_atlas, scene_data.directional_shadow_pixel_size * directional_lights.data[i].soft_shadow_scale * (blur_factor2 + (1.0 - blur_factor2) * float(directional_lights.data[i].blend_splits)), pssm_coord, scene_data.taa_frame_count);
						shadow = mix(shadow, shadow2, pssm_blend);
					}
				}

				shadow = mix(shadow, 1.0, smoothstep(directional_lights.data[i].fade_from, directional_lights.data[i].fade_to, vertex.z)); //done with negative values for performance

#undef BIAS_FUNC
			} // shadows

			if (i < 4) {
				shadow0 |= uint(clamp(shadow * 255.0, 0.0, 255.0)) << (i * 8);
			} else {
				shadow1 |= uint(clamp(shadow * 255.0, 0.0, 255.0)) << ((i - 4) * 8);
			}
		}
#endif // SHADOWS_DISABLED

		for (uint i = 0; i < 8; i++) {
			if (i >= scene_data.directional_light_count) {
				break;
			}

			if (!bool(directional_lights.data[i].mask & instances.data[instance_index].layer_mask)) {
				continue; //not masked
			}

			float shadow = 1.0;
#ifndef SHADOWS_DISABLED
			if (i < 4) {
				shadow = float(shadow0 >> (i * 8u) & 0xFFu) / 255.0;
			} else {
				shadow = float(shadow1 >> ((i - 4u) * 8u) & 0xFFu) / 255.0;
			}

			shadow = mix(1.0, shadow, directional_lights.data[i].shadow_opacity);
#endif

			blur_shadow(shadow);

			float size_A = sc_use_directional_soft_shadows() ? directional_lights.data[i].size : 0.0;

			light_compute(normal, directional_lights.data[i].direction, normalize(view), size_A,
					directional_lights.data[i].color * directional_lights.data[i].energy,
					true, shadow, f0, roughness, metallic, directional_lights.data[i].specular, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
					backlight,
#endif
#ifdef LIGHT_RIM_USED
					rim, rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
					clearcoat, clearcoat_roughness, geo_normal,
#endif // LIGHT_CLEARCOAT_USED
#ifdef LIGHT_ANISOTROPY_USED
					binormal,
					tangent, anisotropy,
#endif
					diffuse_light,
					direct_specular_light);
		}
	}

	{ //omni lights

		uint cluster_omni_offset = cluster_offset;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_omni_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));

		for (uint i = item_from; i < item_to; i++) {
			uint mask = cluster_buffer.data[cluster_omni_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);

			uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);

				if (((1u << bit) & mask) == 0) { //do not process if not originally here
					continue;
				}

				uint light_index = 32 * i + bit;

				if (!bool(omni_lights.data[light_index].mask & instances.data[instance_index].layer_mask)) {
					continue; //not masked
				}

				light_process_omni(light_index, vertex, view, normal, vertex_ddx, vertex_ddy, f0, roughness, metallic, scene_data.taa_frame_count, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
						backlight,
#endif
#ifdef LIGHT_RIM_USED
						rim,
						rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
						clearcoat, clearcoat_roughness, geo_normal,
#endif // LIGHT_CLEARCOAT_USED
#ifdef LIGHT_ANISOTROPY_USED
						binormal, tangent, anisotropy,
#endif
						diffuse_light, direct_specular_light);
			}
		}
	}

	{ //spot lights

		uint cluster_spot_offset = cluster_offset + implementation_data.cluster_type_size;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_spot_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));

		for (uint i = item_from; i < item_to; i++) {
			uint mask = cluster_buffer.data[cluster_spot_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);

			uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);

				if (((1u << bit) & mask) == 0) { //do not process if not originally here
					continue;
				}

				uint light_index = 32 * i + bit;

				if (!bool(spot_lights.data[light_index].mask & instances.data[instance_index].layer_mask)) {
					continue; //not masked
				}

				light_process_spot(light_index, vertex, view, normal, vertex_ddx, vertex_ddy, f0, roughness, metallic, scene_data.taa_frame_count, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
						backlight,
#endif
#ifdef LIGHT_RIM_USED
						rim,
						rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
						clearcoat, clearcoat_roughness, geo_normal,
#endif // LIGHT_CLEARCOAT_USED
#ifdef LIGHT_ANISOTROPY_USED
						binormal, tangent, anisotropy,
#endif
						diffuse_light, direct_specular_light);
			}
		}
	}

	if (sc_cluster_has_area_light()) { // area lights

		uint cluster_area_offset = cluster_offset + implementation_data.cluster_type_size * 2;

		uint item_min;
		uint item_max;
		uint item_from;
		uint item_to;

		cluster_get_item_range(cluster_area_offset + implementation_data.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

		item_from = subgroupBroadcastFirst(subgroupMin(item_from));
		item_to = subgroupBroadcastFirst(subgroupMax(item_to));

		for (uint i = item_from; i < item_to; i++) {
			uint mask = cluster_buffer.data[cluster_area_offset + i];
			mask &= cluster_get_range_clip_mask(i, item_min, item_max);

			uint merged_mask = subgroupBroadcastFirst(subgroupOr(mask));
			while (merged_mask != 0) {
				uint bit = findMSB(merged_mask);
				merged_mask &= ~(1u << bit);

				if (((1u << bit) & mask) == 0) { //do not process if not originally here
					continue;
				}

				uint light_index = 32 * i + bit;

				if (!bool(area_lights.data[light_index].mask & instances.data[instance_index].layer_mask)) {
					continue; //not masked
				}

				light_process_area(light_index, vertex, view, normal, vertex_ddx, vertex_ddy, f0, roughness, metallic, scene_data.taa_frame_count, albedo, alpha, screen_uv, energy_compensation,
#ifdef LIGHT_BACKLIGHT_USED
						backlight,
#endif
#ifdef LIGHT_RIM_USED
						rim,
						rim_tint,
#endif
#ifdef LIGHT_CLEARCOAT_USED
						clearcoat, clearcoat_roughness, geo_normal,
#endif
#ifdef LIGHT_ANISOTROPY_USED
						binormal, tangent, anisotropy,
#endif
						diffuse_light, direct_specular_light);
			}
		}
	}
#endif //!defined(MODE_RENDER_DEPTH) && !defined(MODE_UNSHADED)

#ifdef USE_SHADOW_TO_OPACITY
#ifndef MODE_RENDER_DEPTH
	alpha = min(alpha, clamp(length(ambient_light), 0.0, 1.0));

#if defined(ALPHA_SCISSOR_USED)
	if (alpha < alpha_scissor_threshold) {
		discard;
	}
#endif // ALPHA_SCISSOR_USED

#endif // !MODE_RENDER_DEPTH
#endif // USE_SHADOW_TO_OPACITY

	/////////////////////// OUTPUT //////////////////////////////

#ifdef MODE_RENDER_DEPTH

#ifdef MODE_RENDER_NORMAL_ROUGHNESS
	normal_roughness_output_buffer = vec4(encode24(normal) * 0.5 + 0.5, roughness);

	// We encode the dynamic static into roughness.
	// Values over 0.5 are dynamic, under 0.5 are static.
	normal_roughness_output_buffer.w = normal_roughness_output_buffer.w * (127.0 / 255.0);
	if (bool(instances.data[instance_index].flags & INSTANCE_FLAGS_DYNAMIC)) {
		normal_roughness_output_buffer.w = 1.0 - normal_roughness_output_buffer.w;
	}
#endif //MODE_RENDER_NORMAL_ROUGHNESS

//nothing happens, so a tree-ssa optimizer will result in no fragment shader :)
#else

	// multiply by albedo
	diffuse_light *= albedo; // ambient must be multiplied by albedo at the end

	// apply direct light AO
	diffuse_light *= ao;
	direct_specular_light *= ao;

	// apply metallic
	diffuse_light *= 1.0 - metallic;
	ambient_light *= 1.0 - metallic;

	alpha *= scene_data.pass_alpha_multiplier;

#ifdef MODE_UNSHADED
	frag_color = vec4(albedo, alpha);
#else
	frag_color = vec4(emission + ambient_light + diffuse_light + direct_specular_light + indirect_specular_light, alpha);
#endif //MODE_UNSHADED

#if defined(PREMUL_ALPHA_USED)
	frag_color.rgb *= premul_alpha;
#endif //PREMUL_ALPHA_USED

#endif //MODE_RENDER_DEPTH
}

void main() {
#ifdef MODE_DUAL_PARABOLOID

	if (dp_clip > 0.0) {
		discard;
	}
#endif

	fragment_shader(scene_data_block.data);
}
