#include "visbility_preprocessor.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_device.h"

#include <cstdint>

namespace RendererRD {

VisibilityPreprocessor::VisibilityPreprocessor() {
	Vector<String> sort_modes;
	sort_modes.push_back("\n#define MODE_COUNT\n"); // SORT_MODE_COUNT
	sort_modes.push_back("\n#define MODE_DISPATCH_ARGS\n"); // SORT_MODE_DISPATCH_ARGS
	sort_modes.push_back("\n#define MODE_PREFIX_SUM\n"); // SORT_MODE_PREFIX_SUM
	sort_modes.push_back("\n#define MODE_SORT_PIXELS\n"); // SORT_MODE_SORT_PIXELS

	sort_pass.shader.initialize(sort_modes, "\n#define RECONSTRUCT_LOCAL_SIZE " + itos(RECONSTRUCT_LOCAL_SIZE) + "\n");
	sort_pass.shader_version = sort_pass.shader.version_create();

	for (int i = 0; i < SORT_MODE_MAX; i++) {
		if (sort_pass.shader.is_variant_enabled(i)) {
			sort_pass.pipelines[i].create_compute_pipeline(sort_pass.shader.version_get_shader(sort_pass.shader_version, i));
		}
	}
}

VisibilityPreprocessor::~VisibilityPreprocessor() {
	for (int i = 0; i < SORT_MODE_MAX; i++) {
		sort_pass.pipelines[i].free();
	}
	sort_pass.shader.version_free(sort_pass.shader_version);
}

void VisibilityPreprocessor::sort_allocate_buffers(SortBuffers &p_buffers, const Size2i &p_size, uint32_t p_slot_count) {
	RD *rd = RD::get_singleton();

	if (p_buffers.slot_capacity < p_slot_count || p_buffers.slot_capacity == 0) {
		RID *rids[] = { &p_buffers.slot_counts_buffer, &p_buffers.slot_offsets_buffer, &p_buffers.slot_cursors_buffer, &p_buffers.dispatch_args_buffer };
		for (RID *rid : rids) {
			if (rid->is_valid()) {
				rd->free_rid(*rid);
			}
		}

		p_buffers.slot_capacity = MAX(Math::next_power_of_2(p_slot_count), 64u);

		p_buffers.slot_counts_buffer = rd->storage_buffer_create(p_buffers.slot_capacity * sizeof(uint32_t));
		rd->set_resource_name(p_buffers.slot_counts_buffer, "VSDC slot pixel counts");

		p_buffers.slot_offsets_buffer = rd->storage_buffer_create(p_buffers.slot_capacity * sizeof(uint32_t));
		rd->set_resource_name(p_buffers.slot_offsets_buffer, "VSDC slot pixel list offsets");

		p_buffers.slot_cursors_buffer = rd->storage_buffer_create(p_buffers.slot_capacity * sizeof(uint32_t));
		rd->set_resource_name(p_buffers.slot_cursors_buffer, "VSDC slot scatter cursors");

		p_buffers.dispatch_args_buffer = rd->storage_buffer_create(p_buffers.slot_capacity * DISPATCH_ARGS_STRIDE, {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		rd->set_resource_name(p_buffers.dispatch_args_buffer, "VSDC slot dispatch args");
	}

	// The pixel list is sized by the screen, independently of the slot count.
	const uint32_t pixel_count = MAX(uint32_t(p_size.x * p_size.y), 1u);
	if (p_buffers.pixel_capacity < pixel_count) {
		if (p_buffers.pixel_list_buffer.is_valid()) {
			rd->free_rid(p_buffers.pixel_list_buffer);
		}
		p_buffers.pixel_capacity = pixel_count;
		p_buffers.pixel_list_buffer = rd->storage_buffer_create(p_buffers.pixel_capacity * sizeof(uint32_t[2]));
		rd->set_resource_name(p_buffers.pixel_list_buffer, "VSDC pixel list");
	}

	p_buffers.slot_count = p_slot_count;
}

void VisibilityPreprocessor::sort_free_buffers(SortBuffers &p_buffers) {
	RID *rids[] = { &p_buffers.slot_counts_buffer, &p_buffers.slot_offsets_buffer, &p_buffers.slot_cursors_buffer, &p_buffers.dispatch_args_buffer, &p_buffers.pixel_list_buffer };
	for (RID *rid : rids) {
		if (rid->is_valid()) {
			RD::get_singleton()->free_rid(*rid);
		}
	}
	p_buffers = SortBuffers();
}

// Each pass is its own compute list: the render graph only orders (and barriers) between lists.
void VisibilityPreprocessor::sort_pixels(const SortBuffers &p_buffers, RID p_visibility_buffer, RID p_draw_records, uint32_t p_draw_record_count, const Size2i &p_size) {
	if (p_buffers.slot_count == 0) {
		return; // Nothing in the opaque list, nothing can be covered.
	}

	RD *rd = RD::get_singleton();
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();

	rd->buffer_clear(p_buffers.slot_counts_buffer, 0, p_buffers.slot_count * sizeof(uint32_t));
	rd->buffer_clear(p_buffers.slot_cursors_buffer, 0, p_buffers.slot_count * sizeof(uint32_t));

	SortPushConstant push_constant = {};
	push_constant.screen_size[0] = p_size.x;
	push_constant.screen_size[1] = p_size.y;
	push_constant.slot_count = p_buffers.slot_count;
	push_constant.draw_record_count = p_draw_record_count;

	RD::Uniform u_slot_counts(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, p_buffers.slot_counts_buffer);
	RID sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_visibility(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, p_visibility_buffer }));
	RD::Uniform u_draw_records(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, p_draw_records);
	RD::Uniform u_dispatch_args(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, p_buffers.dispatch_args_buffer);
	RD::Uniform u_slot_offsets(RD::UNIFORM_TYPE_STORAGE_BUFFER, 4, p_buffers.slot_offsets_buffer);
	RD::Uniform u_slot_cursors(RD::UNIFORM_TYPE_STORAGE_BUFFER, 5, p_buffers.slot_cursors_buffer);
	RD::Uniform u_pixel_list(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, p_buffers.pixel_list_buffer);

	//count pixels per material slot
	{
		rd->draw_command_begin_label("Count Pixels Per Material Slot");

		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_COUNT);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_COUNT].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_slot_counts, u_visibility, u_draw_records), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch_threads(compute_list, p_size.x, p_size.y, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}

	//count shading thread groups per material slot
	{
		rd->draw_command_begin_label("Count Shading Groups Per Material Slot");

		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_DISPATCH_ARGS);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_DISPATCH_ARGS].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_slot_counts, u_dispatch_args), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch_threads(compute_list, p_buffers.slot_count, 1, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}

	//compute offsets into the pixel list with an exclusive prefix sum (one workgroup)
	{
		rd->draw_command_begin_label("Compute Material Slot Offsets");

		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_PREFIX_SUM);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_PREFIX_SUM].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_slot_counts, u_slot_offsets), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch(compute_list, 1, 1, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}

	//scatter the pixels into their material slot's range of the pixel list
	{
		rd->draw_command_begin_label("Sort Pixels By Material Slot");

		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_SORT_PIXELS);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_SORT_PIXELS].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_visibility, u_draw_records, u_slot_offsets, u_slot_cursors, u_pixel_list), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch_threads(compute_list, p_size.x, p_size.y, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}
}

} //namespace RendererRD
