#[compute]

#version 450

#VERSION_DEFINES

// Variants (DeferredShader::SortMode):
//   MODE_COUNT:          one thread per pixel, counts the covered pixels of every surface.
//   MODE_DISPATCH_ARGS:  one thread per surface, turns the counts into DispatchIndirectCommand arguments.
// RECONSTRUCT_LOCAL_SIZE comes from DeferredShader::RECONSTRUCT_LOCAL_SIZE.

layout(push_constant, std430) uniform Params {
	uvec2 screen_size;
	uint surface_count; // Number of valid entries in surface_counts.
	uint pad;
}
params;

layout(set = 0, binding = 0, std430) restrict
#ifndef MODE_COUNT
		readonly
#endif
		buffer SurfaceCounts {
	uint data[];
}
surface_counts;

#ifdef MODE_COUNT

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 1) uniform usampler2D visibility_buffer;

void main() {
	uvec2 pixel = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}

	uvec2 payload = texelFetch(visibility_buffer, ivec2(pixel), 0).xy;
	if (payload.x == 0u) {
		return;
	}

	uint surface = payload.y >> 16u;
	if (surface < params.surface_count) {
		atomicAdd(surface_counts.data[surface], 1u);
	}
}

#endif

#ifdef MODE_DISPATCH_ARGS

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Tightly packed DispatchIndirectCommand {x, y, z} per surface.
layout(set = 0, binding = 1, std430) restrict writeonly buffer DispatchArgs {
	uint data[];
}
dispatch_args;

void main() {
	uint surface = gl_GlobalInvocationID.x;
	if (surface >= params.surface_count) {
		return;
	}

	dispatch_args.data[surface * 3u + 0u] = (surface_counts.data[surface] + RECONSTRUCT_LOCAL_SIZE - 1u) / RECONSTRUCT_LOCAL_SIZE;
	dispatch_args.data[surface * 3u + 1u] = 1u;
	dispatch_args.data[surface * 3u + 2u] = 1u;
}

#endif
