#[compute]

#version 450

#VERSION_DEFINES

// Classifies the pixels of the visibility buffer by material slot, slot(pixel) = draw_records[payload.y >> 16].material_slot.
// Variants (VisibilityPreprocessor::SortMode), must be run in this order, each in its own compute list:
//   MODE_COUNT:          one thread per pixel, slot_counts[slot] += 1 for every covered pixel.
//   MODE_DISPATCH_ARGS:  one thread per slot, dispatch_args[slot] = { ceil(count / RECONSTRUCT_LOCAL_SIZE), 1, 1 }.
//   MODE_PREFIX_SUM:     a single workgroup, slot_offsets = exclusive prefix sum of slot_counts.
//   MODE_SORT_PIXELS:    one thread per pixel, writes the pixel into pixel_list[slot_offsets[slot] + cursor].
// slot_counts and slot_offsets stay valid afterwards: the deferred shading pass reads them.
// RECONSTRUCT_LOCAL_SIZE comes from VisibilityPreprocessor::RECONSTRUCT_LOCAL_SIZE.

#include "deferred_records_inc.glsl"

layout(push_constant, std430) uniform Params {
	uvec2 screen_size;
	uint slot_count; // Number of material slots.
	uint draw_record_count; // Number of entries in draw_records.
}
params;

#if defined(MODE_COUNT) || defined(MODE_DISPATCH_ARGS) || defined(MODE_PREFIX_SUM)
layout(set = 0, binding = 0, std430) restrict
#if !defined(MODE_COUNT)
		readonly
#endif
		buffer SlotCounts {
	uint data[];
}
slot_counts;
#endif

#if defined(MODE_COUNT) || defined(MODE_SORT_PIXELS)
layout(set = 0, binding = 1) uniform usampler2D visibility_buffer;

layout(set = 0, binding = 2, std430) restrict readonly buffer DrawRecords {
	DrawRecord data[];
}
draw_records;

// Returns the material slot of a pixel, or 0xFFFFFFFF if it is empty.
uint pixel_slot(uvec2 p_pixel) {
	uvec2 payload = texelFetch(visibility_buffer, ivec2(p_pixel), 0).xy;
	if (payload.x == 0u) {
		return 0xFFFFFFFFu; // Primitive is invalid, and so is the visbuffer entry.
	}
	uint group = payload.y >> 16u;
	if (group >= params.draw_record_count) {
		return 0xFFFFFFFFu;
	}
	uint slot = draw_records.data[group].material_slot;
	return slot < params.slot_count ? slot : 0xFFFFFFFFu;
}
#endif

#if defined(MODE_PREFIX_SUM) || defined(MODE_SORT_PIXELS)
layout(set = 0, binding = 4, std430) restrict
#if defined(MODE_SORT_PIXELS)
		readonly
#endif
		buffer SlotOffsets {
	uint data[];
}
slot_offsets;
#endif

#ifdef MODE_COUNT

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
	uvec2 pixel = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}

	uint slot = pixel_slot(pixel);
	if (slot != 0xFFFFFFFFu) {
		atomicAdd(slot_counts.data[slot], 1u);
	}
}

#endif

#ifdef MODE_DISPATCH_ARGS

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Tightly packed DispatchIndirectCommand {x, y, z} per slot.
layout(set = 0, binding = 3, std430) restrict writeonly buffer DispatchArgs {
	uint data[];
}
dispatch_args;

void main() {
	uint slot = gl_GlobalInvocationID.x;
	if (slot >= params.slot_count) {
		return;
	}

	dispatch_args.data[slot * 3u + 0u] = (slot_counts.data[slot] + RECONSTRUCT_LOCAL_SIZE - 1u) / RECONSTRUCT_LOCAL_SIZE;
	dispatch_args.data[slot * 3u + 1u] = 1u;
	dispatch_args.data[slot * 3u + 2u] = 1u;
}

#endif

#ifdef MODE_PREFIX_SUM

// Dispatched as a single workgroup: scans the slots in chunks of PREFIX_SUM_SIZE (Hillis-Steele in shared memory)
// and carries the running total between chunks. The slot count is expected to be small (unique materials on screen).
#define PREFIX_SUM_SIZE 256u

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

shared uint scan[PREFIX_SUM_SIZE];

void main() {
	uint tid = gl_LocalInvocationID.x;
	uint carry = 0u;

	for (uint base = 0u; base < params.slot_count; base += PREFIX_SUM_SIZE) {
		uint slot = base + tid;
		uint count = slot < params.slot_count ? slot_counts.data[slot] : 0u;
		scan[tid] = count;
		barrier();

		for (uint stride = 1u; stride < PREFIX_SUM_SIZE; stride <<= 1u) {
			uint add = tid >= stride ? scan[tid - stride] : 0u;
			barrier();
			scan[tid] += add;
			barrier();
		}

		if (slot < params.slot_count) {
			slot_offsets.data[slot] = carry + scan[tid] - count;
		}
		carry += scan[PREFIX_SUM_SIZE - 1u];
		barrier();
	}
}

#endif

#ifdef MODE_SORT_PIXELS

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 5, std430) restrict buffer SlotCursors {
	uint data[];
}
slot_cursors;

layout(set = 0, binding = 6, std430) restrict writeonly buffer PixelList {
	uvec2 data[];
}
pixel_list;

void main() {
	uvec2 pixel = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}

	uint slot = pixel_slot(pixel);
	if (slot != 0xFFFFFFFFu) {
		pixel_list.data[slot_offsets.data[slot] + atomicAdd(slot_cursors.data[slot], 1u)] = pixel;
	}
}

#endif
