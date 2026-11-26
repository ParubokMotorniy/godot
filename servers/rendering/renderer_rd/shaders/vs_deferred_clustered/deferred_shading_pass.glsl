#[compute]

#version 450

#VERSION_DEFINES

// Deferred shading pass: dispatched once per material slot (indirect, args from the sort pass), one thread per
// pixel of the slot's range in the pixel list. 

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

#define DEFERRED_SHADING_PASS

#include "../half_inc.glsl"
#include "deferred_records_inc.glsl"
#include "scene_vs_deferred_clustered_inc.glsl"
// TODO: scene_vs_deferred_clustered_lights_inc.glsl is fragment-only for now (the shadow filters read
// gl_FragCoord)

// DEFERRED_LOCAL_SIZE == VisibilityPreprocessor::RECONSTRUCT_LOCAL_SIZE (the sort pass computes the dispatch args with it).
layout(local_size_x = DEFERRED_LOCAL_SIZE, local_size_y = 1, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	uint slot; // Material slot of this dispatch.
	uint draw_record_count;
	uint geometry_record_count;
	uint slot_count;
}
params;

/* Set 1 (render pass), deferred-only bindings, see RenderVsDeferredClustered::_setup_deferred_shading_uniform_set() */

layout(set = 1, binding = 22) uniform utexture2D visibility_buffer;

layout(set = 1, binding = 23, std430) restrict readonly buffer SlotCounts {
	uint data[];
}
slot_counts;

layout(set = 1, binding = 24, std430) restrict readonly buffer SlotOffsets {
	uint data[];
}
slot_offsets;

layout(set = 1, binding = 25, std430) restrict readonly buffer PixelList {
	uvec2 data[];
}
pixel_list;

layout(set = 1, binding = 26, std430) restrict readonly buffer DrawRecords {
	DrawRecord data[];
}
draw_records;

layout(set = 1, binding = 27, std430) restrict readonly buffer GeometryRecords {
	GeometryRecord data[];
}
geometry_records;

layout(rgba16f, set = 1, binding = 28) uniform restrict writeonly image2D color_output;

// Mesh buffers are read as 32-bit words; 16-bit indices and UNORM16 data are unpacked by hand.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer U32Buf {
	uint d[];
};

uint ld(uvec2 p_addr, uint p_word) {
	return U32Buf(p_addr).d[p_word];
}

// Vertex indices of a triangle (list or strip); false for primitives that are not triangles or out of range.
bool fetch_tri(GeometryRecord r, uint p_prim, out uvec3 r_idx) {
	r_idx = uvec3(0u);
	if ((r.flags & GEOM_FLAG_NOT_TRIANGLES) != 0u) {
		return false;
	}
	uvec3 idx;
	if ((r.flags & GEOM_FLAG_STRIP) != 0u) {
		// Vulkan triangle strip: vertices (p, p+1, p+2), winding flipped for odd primitives.
		idx = (p_prim & 1u) == 0u ? uvec3(p_prim, p_prim + 1u, p_prim + 2u) : uvec3(p_prim + 1u, p_prim, p_prim + 2u);
	} else {
		idx = uvec3(p_prim * 3u, p_prim * 3u + 1u, p_prim * 3u + 2u);
	}
	uint limit = (r.flags & GEOM_FLAG_INDEXED) != 0u ? r.index_count : r.vertex_count;
	if (max(idx.x, max(idx.y, idx.z)) >= limit) {
		return false;
	}
	if ((r.flags & GEOM_FLAG_INDEXED) == 0u) {
		r_idx = idx;
		return true;
	}
	for (uint i = 0u; i < 3u; i++) {
		uint k = idx[i];
		if ((r.flags & GEOM_FLAG_INDEX16) != 0u) {
			uint w = ld(r.index_addr, k >> 1u);
			r_idx[i] = (k & 1u) == 0u ? (w & 0xFFFFu) : (w >> 16u);
		} else {
			r_idx[i] = ld(r.index_addr, k);
		}
	}
	return max(r_idx.x, max(r_idx.y, r_idx.z)) < r.vertex_count;
}

// Raw position attribute as the vertex input stage sees it (compressed: UNORM16 xyz in the AABB + tangent angle in w).
vec4 fetch_pos_raw(GeometryRecord r, uint v) {
	uint base = v * (r.position_stride >> 2u);
	if ((r.flags & GEOM_FLAG_COMPRESSED_READ) != 0u) {
		vec2 a = unpackUnorm2x16(ld(r.vertex_addr, base));
		vec2 b = unpackUnorm2x16(ld(r.vertex_addr, base + 1u));
		return vec4(a, b);
	}
	return vec4(uintBitsToFloat(ld(r.vertex_addr, base)), uintBitsToFloat(ld(r.vertex_addr, base + 1u)), uintBitsToFloat(ld(r.vertex_addr, base + 2u)), 1.0);
}

// TODO: normal / UV decode (fetch_normal, fetch_uv_raw in the E1 experiment, using GEOM_FLAG_HAS_NORMAL / HAS_UV /
// COMPRESSED_UV, normal_stride, attrib_stride, uv_offset) belongs to the shading step, not to this template.

void main() {
	uint local = gl_GlobalInvocationID.x;
	if (local >= slot_counts.data[params.slot]) {
		return;
	}
	uint list_index = slot_offsets.data[params.slot] + local;

	uvec2 pixel = pixel_list.data[list_index];
	uvec2 payload = texelFetch(usampler2D(visibility_buffer, SAMPLER_NEAREST_CLAMP), ivec2(pixel), 0).xy;
	uint primitive = payload.x - 1u; // Listed pixels are never empty (payload.x != 0).
	uint group = payload.y >> 16u; // surface_submission_idx: first element of the draw group.
	uint instance_in_draw = payload.y & 0xFFFFu;
	if (group >= params.draw_record_count) {
		return;
	}

	DrawRecord draw = draw_records.data[group];
	if (draw.geometry_idx >= params.geometry_record_count) {
		return;
	}
	GeometryRecord geom = geometry_records.data[draw.geometry_idx];

	// InstanceData slot: implicit instancing gives every member its own slot, a multimesh shares the group's slot
	// and indexes its own buffer (draw.instance_addr, draw.multimesh_offset) with instance_in_draw.
	// TODO: resolve the instance once shading needs it (instances.data[instance_slot], multimesh via BDA).
	bool multimesh = (draw.flags & DRAW_FLAG_MULTIMESH) != 0u;
	uint instance_slot = multimesh ? group : group + instance_in_draw;

	uvec3 tri;
	bool valid = fetch_tri(geom, primitive, tri);
	vec4 p0 = vec4(0.0);
	vec4 p1 = vec4(0.0);
	vec4 p2 = vec4(0.0);
	if (valid) {
		p0 = fetch_pos_raw(geom, tri.x);
		p1 = fetch_pos_raw(geom, tri.y);
		p2 = fetch_pos_raw(geom, tri.z);
	}

	//TODO: barycentrics, attribute interpolation, material evaluation (set 3), lighting and the output image.
	float normalizedSlotIdx = float(params.slot) / float(params.slot_count);
	imageStore(color_output, ivec2(pixel), vec4(normalizedSlotIdx, normalizedSlotIdx, normalizedSlotIdx, 1.0));
}
