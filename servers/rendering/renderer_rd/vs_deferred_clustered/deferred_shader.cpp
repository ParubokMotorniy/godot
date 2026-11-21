#include "deferred_shader.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_device.h"

#include <cstdint>

namespace RendererRD {

DeferredShader::DeferredShader() {
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

DeferredShader::~DeferredShader() {
	for (int i = 0; i < SORT_MODE_MAX; i++) {
		sort_pass.pipelines[i].free();
	}
	sort_pass.shader.version_free(sort_pass.shader_version);
}

void DeferredShader::sort_allocate_buffers(SortBuffers &p_buffers, const Size2i &p_size, uint32_t p_surface_count) {
	if (p_surface_count > MAX_SURFACES) {
		WARN_PRINT_ONCE(vformat("The visibility buffer can address %d surfaces, the opaque list has %d: surfaces from %d on alias lower ones.", MAX_SURFACES, p_surface_count, MAX_SURFACES));
		p_surface_count = MAX_SURFACES;
	}

	if (p_buffers.capacity < p_surface_count || p_buffers.capacity == 0) {
		sort_free_buffers(p_buffers);

		RD *rd = RD::get_singleton();
		p_buffers.capacity = MAX(Math::next_power_of_2(p_surface_count), 64u);

		p_buffers.surface_count_buffer = rd->storage_buffer_create(p_buffers.capacity * sizeof(uint32_t));
		rd->set_resource_name(p_buffers.surface_count_buffer, "VSDC surface pixel counts");

		p_buffers.dispatch_args_buffer = rd->storage_buffer_create(p_buffers.capacity * DISPATCH_ARGS_STRIDE, {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
		rd->set_resource_name(p_buffers.dispatch_args_buffer, "VSDC surface dispatch args");

		p_buffers.pixel_list_buffer = rd->storage_buffer_create(p_size.x * p_size.y * sizeof(uint32_t[2]));
		rd->set_resource_name(p_buffers.pixel_list_buffer, "VSDC pixel lists");
	}

	p_buffers.surface_count = p_surface_count;
}

void DeferredShader::sort_free_buffers(SortBuffers &p_buffers) {
	if (p_buffers.surface_count_buffer.is_valid()) {
		RD::get_singleton()->free_rid(p_buffers.surface_count_buffer);
	}
	if (p_buffers.dispatch_args_buffer.is_valid()) {
		RD::get_singleton()->free_rid(p_buffers.dispatch_args_buffer);
	}
	if(p_buffers.pixel_list_buffer.is_valid())
	{
		RD::get_singleton()->free_rid(p_buffers.pixel_list_buffer);
	}
	p_buffers = SortBuffers();
}

//TODO: I may want to merge these into a single list with explicit barriers
void DeferredShader::sort_pixels(const SortBuffers &p_buffers, RID p_visibility_buffer, const Size2i &p_size) {
	if (p_buffers.surface_count == 0) {
		return; // Nothing in the opaque list, nothing can be covered.
	}

	RD *rd = RD::get_singleton();
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();

	rd->buffer_clear(p_buffers.surface_count_buffer, 0, p_buffers.surface_count * sizeof(uint32_t));

	SortPushConstant push_constant = {};
	push_constant.screen_size[0] = p_size.x;
	push_constant.screen_size[1] = p_size.y;
	push_constant.surface_count = p_buffers.surface_count;

	RD::Uniform u_surface_count(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, p_buffers.surface_count_buffer);
	RID sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_visibility(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, p_visibility_buffer }));

	//count pixels per surface
	{
		rd->draw_command_begin_label("Count Pixels Per Surface");

		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_COUNT);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_COUNT].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_surface_count, u_visibility), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch_threads(compute_list, p_size.x, p_size.y, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}

	//count reconstruction thread groups per surface
	{
		rd->draw_command_begin_label("Count Reconstruction Groups Per Surface");

		RD::Uniform u_dispatch_args(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, p_buffers.dispatch_args_buffer);
		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_DISPATCH_ARGS);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_DISPATCH_ARGS].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_surface_count, u_dispatch_args), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch_threads(compute_list, p_buffers.surface_count, 1, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}

	//compute offsets into pixel lists with prefix sum
	{
		rd->draw_command_begin_label("Compute Surface Offsets");

		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_PREFIX_SUM);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_PREFIX_SUM].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_surface_count), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch_threads(compute_list, p_buffers.surface_count, 1, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}

	//build pixel lists per surface
	{
		rd->draw_command_begin_label("Count pixels Per Surface");

		RD::Uniform u_pixel_lists(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, p_buffers.pixel_list_buffer);
		RID shader = sort_pass.shader.version_get_shader(sort_pass.shader_version, SORT_MODE_SORT_PIXELS);

		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, sort_pass.pipelines[SORT_MODE_SORT_PIXELS].get_rid());
		rd->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_surface_count, u_visibility, u_pixel_lists), 0);
		rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(SortPushConstant));
		rd->compute_list_dispatch_threads(compute_list, p_size.x, p_size.y, 1);
		rd->compute_list_end();

		rd->draw_command_end_label();
	}
}

} //namespace RendererRD
