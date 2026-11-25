#pragma once

#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shaders/vs_deferred_clustered/visbility_preprocessing_passes.glsl.gen.h"

namespace RendererRD {

class VisibilityPreprocessor {
public:
	VisibilityPreprocessor();
	~VisibilityPreprocessor();

	// Classifies the pixels of the visibility buffer by material slot (pixel -> draw group -> DrawRecord.material_slot):
	//   1. clear slot_counts and slot_cursors,
	//   2. SORT_MODE_COUNT: slot_counts[slot(pixel)] += 1 for every covered pixel,
	//   3. SORT_MODE_DISPATCH_ARGS: dispatch_args[slot] = { ceil(count / RECONSTRUCT_LOCAL_SIZE), 1, 1 },
	//   4. SORT_MODE_PREFIX_SUM: slot_offsets = exclusive prefix sum of slot_counts (single workgroup),
	//   5. SORT_MODE_SORT_PIXELS: write pixel coordinates into the slot's range [offset, offset + count) of the pixel list.
	// slot_counts / slot_offsets / dispatch_args / pixel_list are the inputs of the deferred shading pass.

	struct SortBuffers {
		RID slot_counts_buffer;
		RID slot_offsets_buffer;
		RID slot_cursors_buffer;
		RID dispatch_args_buffer;
		RID pixel_list_buffer;
		uint32_t slot_capacity = 0;
		uint32_t pixel_capacity = 0;
		uint32_t slot_count = 0;
	};

	void sort_allocate_buffers(SortBuffers &p_buffers, const Size2i &p_size, uint32_t p_slot_count);
	static void sort_free_buffers(SortBuffers &p_buffers);
	void sort_pixels(const SortBuffers &p_buffers, RID p_visibility_buffer, RID p_draw_records, uint32_t p_draw_record_count, const Size2i &p_size);

	// Work group size of the deferred shading kernel, one thread per pixel list entry.
	static constexpr uint32_t RECONSTRUCT_LOCAL_SIZE = 64;
	// Bytes per slot in the dispatch argument buffer (DispatchIndirectCommand).
	static constexpr uint32_t DISPATCH_ARGS_STRIDE = 3 * sizeof(uint32_t);

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
		uint32_t slot_count;
		uint32_t draw_record_count;
	};

	struct SortPass {
		VisbilityPreprocessingPassesShaderRD shader;
		RID shader_version;
		PipelineDeferredRD pipelines[SORT_MODE_MAX];
	} sort_pass;
};

} //namespace RendererRD
