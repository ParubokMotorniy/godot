#define M_PI 3.14159265359
#define M_TAU 6.28318530718

#define MAX_VIEWS 2 // Required by the shared SceneData layout, only view 0 is used.

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

#include "../cluster_data_inc.glsl"
#include "../oct_inc.glsl"
#include "../scene_data_inc.glsl"

#if !defined(MODE_RENDER_DEPTH) || defined(MODE_RENDER_NORMAL_ROUGHNESS) || defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(BENT_NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
#ifndef NORMAL_USED
#define NORMAL_USED
#endif
#endif

#if !defined(TANGENT_USED) && (defined(NORMAL_MAP_USED) || defined(BENT_NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED))
#define TANGENT_USED
#endif

layout(push_constant, std430) uniform DrawCall {
	uint instance_index;
	uint multimesh_offset; // Offset of the current frame's transforms in a multimesh/particles buffer.
	uint pad0;
	uint pad1;
}
draw_call;

/* Specialization Constants (set per pipeline, see SceneShaderVsDeferredClustered::ShaderSpecialization) */

layout(constant_id = 0) const uint sc_packed_0 = 0;

bool sc_multimesh() {
	return ((sc_packed_0 >> 0) & 1U) != 0;
}

bool sc_multimesh_format_2d() {
	return ((sc_packed_0 >> 1) & 1U) != 0;
}

bool sc_multimesh_has_color() {
	return ((sc_packed_0 >> 2) & 1U) != 0;
}

bool sc_multimesh_has_custom_data() {
	return ((sc_packed_0 >> 3) & 1U) != 0;
}

#define INSTANCE_INDEX gl_InstanceIndex

/* Set 0: Base Pass (never changes) */

#include "../light_data_inc.glsl"

layout(set = 0, binding = 0) uniform sampler shadow_sampler;

#define INSTANCE_FLAGS_NON_UNIFORM_SCALE (1 << 4)
#define INSTANCE_FLAGS_FADE_SHIFT 24

#define SCREEN_SPACE_EFFECTS_FLAGS_USE_SSAO (1 << 0)

layout(set = 0, binding = 1, std430) restrict readonly buffer OmniLights {
	LightData data[];
}
omni_lights;

layout(set = 0, binding = 2, std430) restrict readonly buffer SpotLights {
	LightData data[];
}
spot_lights;

layout(set = 0, binding = 3, std140) uniform DirectionalLights {
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
}
directional_lights;

layout(set = 0, binding = 4) uniform sampler DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;

layout(set = 0, binding = 5) uniform texture2D best_fit_normal_texture;

layout(set = 0, binding = 6) uniform texture2D dfg;

/* Set 1: Render Pass (changes per render pass) */

layout(set = 1, binding = 0, std140) uniform SceneDataBlock {
	SceneData data;
	SceneData prev_data;
}
scene_data_block;

struct ImplementationData {
	uint cluster_shift;
	uint cluster_width;
	uint cluster_type_size;
	uint max_cluster_element_count_div_32;

	uint ss_effects_flags;
	float ssao_light_affect;
	float ssao_ao_affect;
	uint pad1;
};

layout(set = 1, binding = 1, std140) uniform ImplementationDataBlock {
	ImplementationData data;
}
implementation_data_block;

#define implementation_data implementation_data_block.data

struct InstanceData {
	mat3x4 transform;
	vec4 compressed_aabb_position_pad; // Only .xyz is used. .w is padding.
	vec4 compressed_aabb_size_pad; // Only .xyz is used. .w is padding.
	vec4 uv_scale;
	uint flags;
	uint layer_mask;
	uint pad0;
	uint pad1;
#ifdef USE_DOUBLE_PRECISION
	vec4 model_precision;
#endif
};

layout(set = 1, binding = 2, std430) buffer restrict readonly InstanceDataBuffer {
	InstanceData data[];
}
instances;

#ifdef USE_RADIANCE_OCTMAP_ARRAY

layout(set = 1, binding = 3) uniform texture2DArray radiance_octmap;

#else

layout(set = 1, binding = 3) uniform texture2D radiance_octmap;

#endif

layout(set = 1, binding = 4) uniform texture2D shadow_atlas;

layout(set = 1, binding = 5) uniform texture2D directional_shadow_atlas;

layout(set = 1, binding = 6, std430) buffer restrict readonly ClusterBuffer {
	uint data[];
}
cluster_buffer;

layout(set = 1, binding = 7 + 0) uniform sampler SAMPLER_NEAREST_CLAMP;
layout(set = 1, binding = 7 + 1) uniform sampler SAMPLER_LINEAR_CLAMP;
layout(set = 1, binding = 7 + 2) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_CLAMP;
layout(set = 1, binding = 7 + 3) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;
layout(set = 1, binding = 7 + 4) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 1, binding = 7 + 5) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 1, binding = 7 + 6) uniform sampler SAMPLER_NEAREST_REPEAT;
layout(set = 1, binding = 7 + 7) uniform sampler SAMPLER_LINEAR_REPEAT;
layout(set = 1, binding = 7 + 8) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_REPEAT;
layout(set = 1, binding = 7 + 9) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT;
layout(set = 1, binding = 7 + 10) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_REPEAT;
layout(set = 1, binding = 7 + 11) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_REPEAT;

// Referenced by BaseMaterial3D proximity fade / refraction (hint_depth_texture / hint_screen_texture), always default textures.
layout(set = 1, binding = 19) uniform texture2D depth_buffer;
layout(set = 1, binding = 20) uniform texture2D color_buffer;
layout(set = 1, binding = 21) uniform texture2D ao_buffer;

// https://google.github.io/filament/Filament.html#toc5.3.4.7
// Note: The roughness value is inverted
vec3 prefiltered_dfg(float lod, float NoV) {
	return textureLod(sampler2D(dfg, SAMPLER_LINEAR_CLAMP), vec2(NoV, 1.0 - lod), 0.0).rgb;
}

// Compute multiscatter compensation
// https://google.github.io/filament/Filament.html#listing_energycompensationimpl
vec3 get_energy_compensation(vec3 f0, float env) {
	return 1.0 + f0 * (1.0 / env - 1.0);
}

/* Set 2 Skeleton & Instancing (can change per item) */

layout(set = 2, binding = 0, std430) restrict readonly buffer Transforms {
	vec4 data[];
}
transforms;

/* Set 3 User Material */
