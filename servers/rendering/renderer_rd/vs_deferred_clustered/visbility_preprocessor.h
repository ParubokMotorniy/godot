#pragma once

#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shaders/vs_deferred_clustered/visbility_preprocessing_passes.glsl.gen.h"

namespace RendererRD {

class VisibilityPreprocessor {
public:
	VisibilityPreprocessor();
	~VisibilityPreprocessor();

	// Classifies the pixels of the visibility buffer by surface (index into the opaque render list):
	//   1. clear surface_count[0, surface_count),
	//   2. MODE_COUNT: surface_count[surface(pixel)] += 1 for every covered pixel,
	//   3. MODE_DISPATCH_ARGS: dispatch_args[surface] = { ceil(count / RECONSTRUCT_LOCAL_SIZE), 1, 1 }.
	//   4. SORT_MODE_PREFIX_SUM: computes offsets of surfaces into pixel list buffer
	//   5. SORT_MODE_SORT_PIXELS: write pixel coordinates into corresponding surface's section in pixel list buffers

	struct SortBuffers {
		RID surface_count_buffer;
		RID dispatch_args_buffer;
		RID pixel_list_buffer;
		uint32_t capacity = 0;
		uint32_t surface_count = 0;
	};

	void sort_allocate_buffers(SortBuffers &p_buffers, const Size2i &p_size, uint32_t p_surface_count);
	static void sort_free_buffers(SortBuffers &p_buffers);
	void sort_pixels(const SortBuffers &p_buffers, RID p_visibility_buffer, const Size2i &p_size);

private:
	enum SortMode {
		SORT_MODE_COUNT,
		SORT_MODE_DISPATCH_ARGS,
		SORT_MODE_PREFIX_SUM,
		SORT_MODE_SORT_PIXELS,
		SORT_MODE_MAX
	};

	struct SortPushConstant {
		uint32_t screen_size[2];
		uint32_t surface_count;
		uint32_t pad;
	};

	struct SortPass {
		VisbilityPreprocessingPassesShaderRD shader;
		RID shader_version;
		PipelineDeferredRD pipelines[SORT_MODE_MAX];
	} sort_pass;

	// Work group size of the reconstruction kernel
	static constexpr uint32_t RECONSTRUCT_LOCAL_SIZE = 64;
	// The visibility payload stores the surface index in the upper 16 bits of .y.
	static constexpr uint32_t MAX_SURFACES = 1 << 16;
	// Bytes per surface in the dispatch argument buffer (DispatchIndirectCommand).
	static constexpr uint32_t DISPATCH_ARGS_STRIDE = 3 * sizeof(uint32_t);
};

} //namespace RendererRD
