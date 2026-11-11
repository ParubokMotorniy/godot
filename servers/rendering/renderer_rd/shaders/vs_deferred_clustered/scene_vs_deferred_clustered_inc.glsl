#define M_PI 3.14159265359
#define M_TAU 6.28318530718
#define ROUGHNESS_MAX_LOD 5

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
layout(constant_id = 1) const uint sc_packed_1 = 0;

bool sc_use_light_projector() {
	return ((sc_packed_0 >> 0) & 1U) != 0;
}

bool sc_use_light_soft_shadows() {
	return ((sc_packed_0 >> 1) & 1U) != 0;
}

bool sc_use_directional_soft_shadows() {
	return ((sc_packed_0 >> 2) & 1U) != 0;
}

bool sc_projector_use_mipmaps() {
	return ((sc_packed_0 >> 3) & 1U) != 0;
}

uint sc_soft_shadow_samples() {
	return (sc_packed_0 >> 8) & 63U;
}

uint sc_penumbra_shadow_samples() {
	return (sc_packed_0 >> 14) & 63U;
}

uint sc_directional_soft_shadow_samples() {
	return (sc_packed_0 >> 20) & 63U;
}

uint sc_directional_penumbra_shadow_samples() {
	return (sc_packed_0 >> 26) & 63U;
}

bool sc_multimesh() {
	return ((sc_packed_1 >> 0) & 1U) != 0;
}

bool sc_multimesh_format_2d() {
	return ((sc_packed_1 >> 1) & 1U) != 0;
}

bool sc_multimesh_has_color() {
	return ((sc_packed_1 >> 2) & 1U) != 0;
}

bool sc_multimesh_has_custom_data() {
	return ((sc_packed_1 >> 3) & 1U) != 0;
}

bool sc_cluster_has_area_light() {
	return ((sc_packed_1 >> 4) & 1U) != 0;
}

float sc_luminance_multiplier() {
	// Not used in clustered renderer but we share some code with the mobile renderer that requires this.
	return 1.0;
}

#define VERTEX_INDEX gl_VertexIndex
#define INSTANCE_INDEX gl_InstanceIndex

#define REFLECTION_MULTIPLIER 1.0

/* Set 0: Base Pass (never changes) */

#include "../light_data_inc.glsl"

layout(set = 0, binding = 2) uniform sampler shadow_sampler;

#define INSTANCE_FLAGS_DYNAMIC (1 << 3)
#define INSTANCE_FLAGS_NON_UNIFORM_SCALE (1 << 4)
#define INSTANCE_FLAGS_PARTICLES (1 << 11)
#define INSTANCE_FLAGS_PARTICLE_TRAIL_SHIFT 16
#define INSTANCE_FLAGS_FADE_SHIFT 24
//3 bits of stride
#define INSTANCE_FLAGS_PARTICLE_TRAIL_MASK 0xFF

#define SCREEN_SPACE_EFFECTS_FLAGS_USE_SSAO (1 << 0)

layout(set = 0, binding = 3, std430) restrict readonly buffer OmniLights {
	LightData data[];
}
omni_lights;

layout(set = 0, binding = 4, std430) restrict readonly buffer SpotLights {
	LightData data[];
}
spot_lights;

layout(set = 0, binding = 5, std430) restrict readonly buffer AreaLights {
	LightData data[];
}
area_lights;

// Dummy: required by the shared scene_forward_lights_inc.glsl (reflection_process()), reflection probes are not rendered.
layout(set = 0, binding = 6, std430) restrict readonly buffer ReflectionProbeData {
	ReflectionData data[];
}
reflections;

layout(set = 0, binding = 7, std140) uniform DirectionalLights {
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
}
directional_lights;

// Light projector textures live in the decal atlas.
layout(set = 0, binding = 11) uniform texture2D decal_atlas_srgb;

layout(set = 0, binding = 13, std430) restrict readonly buffer GlobalShaderUniformData {
	vec4 data[];
}
global_shader_uniforms;

layout(set = 0, binding = 15) uniform sampler DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;

layout(set = 0, binding = 16) uniform texture2D best_fit_normal_texture;

layout(set = 0, binding = 17) uniform texture2D dfg;

layout(set = 0, binding = 18) uniform sampler2D ltc_lut1;

layout(set = 0, binding = 19) uniform sampler2D ltc_lut2;

layout(set = 0, binding = 20) uniform texture2D area_light_atlas;

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
	uint instance_uniforms_ofs; //base offset in global buffer for instance variables
	uint layer_mask;
	uint pad;
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

// Dummy: required by the shared scene_forward_lights_inc.glsl (reflection_process()), always a black texture.
layout(set = 1, binding = 4) uniform texture2DArray reflection_atlas;

layout(set = 1, binding = 5) uniform texture2D shadow_atlas;

layout(set = 1, binding = 6) uniform texture2D directional_shadow_atlas;

layout(set = 1, binding = 9, std430) buffer restrict readonly ClusterBuffer {
	uint data[];
}
cluster_buffer;

layout(set = 1, binding = 11) uniform sampler light_projector_sampler;

layout(set = 1, binding = 12 + 0) uniform sampler SAMPLER_NEAREST_CLAMP;
layout(set = 1, binding = 12 + 1) uniform sampler SAMPLER_LINEAR_CLAMP;
layout(set = 1, binding = 12 + 2) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_CLAMP;
layout(set = 1, binding = 12 + 3) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;
layout(set = 1, binding = 12 + 4) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 1, binding = 12 + 5) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 1, binding = 12 + 6) uniform sampler SAMPLER_NEAREST_REPEAT;
layout(set = 1, binding = 12 + 7) uniform sampler SAMPLER_LINEAR_REPEAT;
layout(set = 1, binding = 12 + 8) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_REPEAT;
layout(set = 1, binding = 12 + 9) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT;
layout(set = 1, binding = 12 + 10) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_REPEAT;
layout(set = 1, binding = 12 + 11) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_REPEAT;

// Dummy: referenced by user shaders using hint_depth_texture / hint_screen_texture (always default textures).
layout(set = 1, binding = 24) uniform texture2D depth_buffer;
layout(set = 1, binding = 25) uniform texture2D color_buffer;
// Referenced by user shaders using hint_normal_roughness_texture.
layout(set = 1, binding = 26) uniform texture2D normal_roughness_buffer;
layout(set = 1, binding = 27) uniform texture2D ao_buffer;
#define multiviewSampler sampler2D

vec4 normal_roughness_compatibility(vec4 p_normal_roughness) {
	float roughness = p_normal_roughness.w;
	if (roughness > 0.5) {
		roughness = 1.0 - roughness;
	}
	roughness /= (127.0 / 255.0);
	return vec4(normalize(p_normal_roughness.xyz * 2.0 - 1.0) * 0.5 + 0.5, roughness);
}

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
