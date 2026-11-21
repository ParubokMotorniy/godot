#[compute]

#version 450

#VERSION_DEFINES

// Variants (VisibilityPreprocessor::SortMode):
//   MODE_COUNT:          one thread per pixel, counts the covered pixels of every surface.
//   MODE_DISPATCH_ARGS:  one thread per surface, turns the counts into DispatchIndirectCommand arguments.
// RECONSTRUCT_LOCAL_SIZE comes from VisibilityPreprocessor::RECONSTRUCT_LOCAL_SIZE.

layout(push_constant, std430) uniform Params {
	uvec2 screen_size;
	uint surface_count; // Number of valid entries in surface_counts.
	uint pad;
}
params;

layout(set = 0, binding = 0, std430) restrict
#if defined(MODE_DISPATCH_ARGS)
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
	//primitive is invalid, and so is the visbuffer entry
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


#ifdef MODE_PREFIX_SUM

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

void main() {
	uint surface = gl_GlobalInvocationID.x;
	if (surface >= params.surface_count || surface_counts.data[surface] == 0) {
		return;
	}

	//TODO: crazy thread divergence here. May want to make prefix sum smarter.
	uint localPixelCount = 0;
	for(int i = 0; i < surface; ++i)
	{
		localPixelCount += surface_counts.data[i];
	}

	barrier();

	surface_counts.data[surface] = localPixelCount;
}

#endif

#ifdef MODE_SORT_PIXELS

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 1) uniform usampler2D visibility_buffer;

layout(set = 0, binding = 2, std430) restrict
buffer PixelCounts {
	uvec2 data[];
}
pixel_counts;

void main() {
	uvec2 pixel = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pixel, params.screen_size))) {
		return;
	}

	uvec2 payload = texelFetch(visibility_buffer, ivec2(pixel), 0).xy;
	//primitive is invalid, and so is the visbuffer entry
	if (payload.x == 0u) {
		return;
	}

	uint surface = payload.y >> 16u;
	if (surface < params.surface_count) {
		//write the pixel to surface's list
		pixel_counts.data[atomicAdd(surface_counts.data[surface], 1u)] = pixel;
	}
}

#endif