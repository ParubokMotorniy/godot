/**************************************************************************/
/*  render_vs_deferred_clustered.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "render_vs_deferred_clustered.h"

#include "core/config/project_settings.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_device.h"

using namespace RendererSceneRenderImplementation;

#define FADE_ALPHA_PASS_THRESHOLD 0.999

/* Render buffers */

void RenderVsDeferredClustered::RenderBufferDataVsDeferredClustered::ensure_normal_roughness_texture() {
	ERR_FAIL_NULL(render_buffers);

	if (!render_buffers->has_texture(RB_SCOPE_VS_DEFERRED_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS)) {
		render_buffers->create_texture(RB_SCOPE_VS_DEFERRED_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS, RD::DATA_FORMAT_R8G8B8A8_UNORM, RenderSceneBuffersRD::get_color_usage_bits(false, false, render_buffers->get_can_be_storage()));
	}
}

void RenderVsDeferredClustered::RenderBufferDataVsDeferredClustered::free_data() {
	// JIC, should already have been cleared
	if (render_buffers) {
		render_buffers->clear_context(RB_SCOPE_VS_DEFERRED_CLUSTERED);
		render_buffers->clear_context(RB_SCOPE_SSDS);
		render_buffers->clear_context(RB_SCOPE_SSAO);
	}

	if (cluster_builder) {
		memdelete(cluster_builder);
		cluster_builder = nullptr;
	}
}

void RenderVsDeferredClustered::RenderBufferDataVsDeferredClustered::configure(RenderSceneBuffersRD *p_render_buffers) {
	if (render_buffers) {
		// JIC
		free_data();
	}

	render_buffers = p_render_buffers;
	ERR_FAIL_NULL(render_buffers);

	if (cluster_builder == nullptr) {
		cluster_builder = memnew(ClusterBuilderRD);
	}
	cluster_builder->set_shared(RenderVsDeferredClustered::get_singleton()->get_cluster_builder_shared());

	RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	cluster_builder->setup(p_render_buffers->get_internal_size(), p_render_buffers->get_max_cluster_elements(), p_render_buffers->get_depth_texture(), sampler, p_render_buffers->get_internal_texture());
}

RID RenderVsDeferredClustered::RenderBufferDataVsDeferredClustered::get_color_fb() {
	ERR_FAIL_NULL_V(render_buffers, RID());
	return FramebufferCacheRD::get_singleton()->get_cache(render_buffers->get_internal_texture(), render_buffers->get_depth_texture());
}

RID RenderVsDeferredClustered::RenderBufferDataVsDeferredClustered::get_depth_fb(bool p_normal_roughness) {
	ERR_FAIL_NULL_V(render_buffers, RID());

	if (p_normal_roughness) {
		ensure_normal_roughness_texture();
		return FramebufferCacheRD::get_singleton()->get_cache(render_buffers->get_depth_texture(), get_normal_roughness());
	}
	return FramebufferCacheRD::get_singleton()->get_cache(render_buffers->get_depth_texture());
}

void RenderVsDeferredClustered::RenderSceneBuffersNoMSAA::configure(const RenderSceneBuffersConfiguration *p_config) {
	Ref<RenderSceneBuffersConfiguration> config;
	config.instantiate();
	config->set_render_target(p_config->get_render_target());
	config->set_internal_size(p_config->get_internal_size());
	config->set_target_size(p_config->get_target_size());
	config->set_view_count(p_config->get_view_count());
	config->set_scaling_3d_mode(p_config->get_scaling_3d_mode());
	config->set_msaa_3d(RSE::VIEWPORT_MSAA_DISABLED);
	config->set_screen_space_aa(p_config->get_screen_space_aa());
	config->set_fsr_sharpness(p_config->get_fsr_sharpness());
	config->set_texture_mipmap_bias(p_config->get_texture_mipmap_bias());
	config->set_anisotropic_filtering_level(p_config->get_anisotropic_filtering_level());
	config->set_use_taa(p_config->get_use_taa());
	config->set_use_debanding(p_config->get_use_debanding());
	RenderSceneBuffersRD::configure(config.ptr());
}

Ref<RenderSceneBuffers> RenderVsDeferredClustered::render_buffers_create() {
	// Same as RendererSceneRenderRD::render_buffers_create(), minus VRS, using the MSAA-less buffers.
	Ref<RenderSceneBuffersNoMSAA> rb;
	rb.instantiate();
	rb->set_can_be_storage(_render_buffers_can_be_storage());
	rb->set_max_cluster_elements(get_max_elements());
	rb->set_preferred_data_format(_render_buffers_get_preferred_color_format());
	setup_render_buffer_data(rb);
	return rb;
}

void RenderVsDeferredClustered::setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) {
	Ref<RenderBufferDataVsDeferredClustered> data;
	data.instantiate();
	p_render_buffers->set_custom_data(RB_SCOPE_VS_DEFERRED_CLUSTERED, data);
}

/// RENDERING ///

void RenderVsDeferredClustered::_render_list(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	RenderingDevice *rd = RD::get_singleton();
	RD::DrawListID draw_list = p_draw_list;

	//global scope bindings
	rd->draw_list_bind_uniform_set(draw_list, render_base_uniform_set, SCENE_UNIFORM_SET);
	rd->draw_list_bind_uniform_set(draw_list, p_params->render_pass_uniform_set, RENDER_PASS_UNIFORM_SET);
	rd->draw_list_bind_uniform_set(draw_list, scene_shader.default_vec4_xform_uniform_set, TRANSFORMS_UNIFORM_SET);

	const PassMode pass_mode = p_params->pass_mode;
	const bool color_pass = pass_mode == PASS_MODE_COLOR;
	const bool shadow_pass = pass_mode == PASS_MODE_SHADOW || pass_mode == PASS_MODE_SHADOW_DP;

	SceneShaderVsDeferredClustered::ShaderData::PipelineKey pipeline_key;
	pipeline_key.framebuffer_format_id = p_framebuffer_format;
	pipeline_key.color_pass_flags = (color_pass && (p_params->color_pass_flags & COLOR_PASS_FLAG_TRANSPARENT)) ? SceneShaderVsDeferredClustered::PIPELINE_COLOR_PASS_FLAG_TRANSPARENT : 0;
	switch (pass_mode) {
		case PASS_MODE_COLOR:
			pipeline_key.version = SceneShaderVsDeferredClustered::PIPELINE_VERSION_COLOR_PASS;
			break;
		case PASS_MODE_SHADOW:
		case PASS_MODE_DEPTH:
			pipeline_key.version = SceneShaderVsDeferredClustered::PIPELINE_VERSION_DEPTH_PASS;
			break;
		case PASS_MODE_SHADOW_DP:
			pipeline_key.version = SceneShaderVsDeferredClustered::PIPELINE_VERSION_DEPTH_PASS_DP;
			break;
		case PASS_MODE_DEPTH_NORMAL_ROUGHNESS:
			pipeline_key.version = SceneShaderVsDeferredClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS;
			break;
	}

	RID prev_material_uniform_set;
	RID prev_vertex_array_rd;
	RID prev_index_array_rd;
	RID prev_xforms_uniform_set;
	RID prev_pipeline_rd;

	SceneState::PushConstant push_constant = {};

	for (uint32_t i = p_from_element; i < p_to_element; i++) {
		const GeometryInstanceSurfaceDataCache *surf = p_params->elements[i];
		const RenderElementInfo &element_info = p_params->element_info[i];

		if (color_pass && surf->color_pass_inclusion_mask && (p_params->color_pass_flags & surf->color_pass_inclusion_mask) == 0) {
			// Surfaces can be in both lists (e.g. alpha with depth pre-pass); exclude them from the color pass they don't belong to.
			continue;
		}

		if (surf->owner->instance_count == 0) {
			continue;
		}

		push_constant.base_index = i + p_params->element_offset;

		RID material_uniform_set;
		SceneShaderVsDeferredClustered::ShaderData *shader;
		void *mesh_surface;

		if (shadow_pass || pass_mode == PASS_MODE_DEPTH) { // Plain depth can use the (possibly shared) shadow material.
			material_uniform_set = surf->material_uniform_set_shadow;
			shader = surf->shader_shadow;
			mesh_surface = surf->surface_shadow;
		} else {
			material_uniform_set = surf->material_uniform_set;
			shader = surf->shader;
			mesh_surface = surf->surface;
			if (color_pass) {
				surf->material->set_as_used();
			}
		}

		if (!mesh_surface) {
			continue;
		}

		// Determine the cull variant.
		SceneShaderVsDeferredClustered::ShaderData::CullVariant cull_variant;
		if (shadow_pass && (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_DOUBLE_SIDED_SHADOWS)) {
			cull_variant = SceneShaderVsDeferredClustered::ShaderData::CULL_VARIANT_DOUBLE_SIDED;
		} else {
			bool mirror = surf->owner->mirror != p_params->reverse_cull;
			cull_variant = mirror ? SceneShaderVsDeferredClustered::ShaderData::CULL_VARIANT_REVERSED : SceneShaderVsDeferredClustered::ShaderData::CULL_VARIANT_NORMAL;
		}

		pipeline_key.primitive_type = surf->primitive;
		pipeline_key.cull_mode = shader->get_cull_mode_from_cull_variant(cull_variant);

		SceneShaderVsDeferredClustered::ShaderSpecialization &specialization = pipeline_key.shader_specialization;
		specialization = {};
		specialization.multimesh = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH);
		specialization.multimesh_format_2d = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D);
		specialization.multimesh_has_color = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR);
		specialization.multimesh_has_custom_data = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA);

		// Skeleton and blend shape.
		RID vertex_array_rd;
		RD::VertexFormatID vertex_format = -1;
		uint64_t input_mask = shader->get_vertex_input_mask(pipeline_key.version);
		if (surf->owner->mesh_instance.is_valid()) {
			mesh_storage->mesh_instance_surface_get_vertex_arrays_and_format(surf->owner->mesh_instance, surf->surface_index, input_mask, false, false, vertex_array_rd, vertex_format);
		} else {
			mesh_storage->mesh_surface_get_vertex_arrays_and_format(mesh_surface, input_mask, false, false, vertex_array_rd, vertex_format);
		}
		pipeline_key.vertex_format_id = vertex_format;

		// Pipelines are compiled synchronously the first time they are needed.
		RID pipeline_rd = shader->pipeline_hash_map.get_pipeline(pipeline_key, pipeline_key.hash(), true, RSE::PIPELINE_SOURCE_DRAW);

		if (pipeline_rd.is_valid()) {
			RID index_array_rd = mesh_storage->mesh_surface_get_index_array(mesh_surface, element_info.lod_index);

			if (prev_vertex_array_rd != vertex_array_rd) {
				rd->draw_list_bind_vertex_array(draw_list, vertex_array_rd);
				prev_vertex_array_rd = vertex_array_rd;
			}

			if (prev_index_array_rd != index_array_rd) {
				if (index_array_rd.is_valid()) {
					rd->draw_list_bind_index_array(draw_list, index_array_rd);
				}
				prev_index_array_rd = index_array_rd;
			}

			if (prev_pipeline_rd != pipeline_rd) {
				rd->draw_list_bind_render_pipeline(draw_list, pipeline_rd);
				prev_pipeline_rd = pipeline_rd;
			}

			RID xforms_uniform_set = surf->owner->transforms_uniform_set;
			if (xforms_uniform_set.is_valid() && prev_xforms_uniform_set != xforms_uniform_set) {
				rd->draw_list_bind_uniform_set(draw_list, xforms_uniform_set, TRANSFORMS_UNIFORM_SET);
				prev_xforms_uniform_set = xforms_uniform_set;
			}

			if (material_uniform_set != prev_material_uniform_set) {
				// Update uniform set.
				if (material_uniform_set.is_valid() && rd->uniform_set_is_valid(material_uniform_set)) { // Material may not have a uniform set.
					rd->draw_list_bind_uniform_set(draw_list, material_uniform_set, MATERIAL_UNIFORM_SET);
				}

				prev_material_uniform_set = material_uniform_set;
			}

			// Multimesh buffers may be double buffered (for motion vectors), get the offset of the current frame.
			uint32_t unused_previous_offset = 0;
			if (surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH) {
				mesh_storage->_multimesh_get_motion_vectors_offsets(surf->owner->data->base, push_constant.multimesh_offset, unused_previous_offset);
			} else {
				push_constant.multimesh_offset = 0;
			}

			rd->draw_list_set_push_constant(draw_list, &push_constant, sizeof(SceneState::PushConstant));

			uint32_t instance_count = surf->owner->instance_count > 1 ? surf->owner->instance_count : element_info.repeat;

			if (surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT) {
				rd->draw_list_draw_indirect(draw_list, index_array_rd.is_valid(), mesh_storage->_multimesh_get_command_buffer_rd_rid(surf->owner->data->base), surf->surface_index * sizeof(uint32_t) * mesh_storage->INDIRECT_MULTIMESH_COMMAND_STRIDE, 1, 0);
			} else {
				rd->draw_list_draw(draw_list, index_array_rd.is_valid(), instance_count);
			}
		}

		i += element_info.repeat - 1; //skip equal elements
	}
}

void RenderVsDeferredClustered::_render_list_with_draw_list(RenderListParameters *p_params, RID p_framebuffer, BitField<RD::DrawFlags> p_draw_flags, const Vector<Color> &p_clear_color_values, float p_clear_depth_value, const Rect2 &p_region) {
	RD::FramebufferFormatID fb_format = RD::get_singleton()->framebuffer_get_format(p_framebuffer);
	RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_framebuffer, p_draw_flags, p_clear_color_values, p_clear_depth_value, 0, p_region);
	_render_list(draw_list, fb_format, p_params, 0, p_params->element_count);
	RD::get_singleton()->draw_list_end();
}

uint32_t RenderVsDeferredClustered::_setup_environment(const RenderDataRD *p_render_data, const Size2i &p_screen_size, const Size2 &p_viewport_size, const Color &p_default_bg_color, bool p_opaque_render_buffers, bool p_pancake_shadows) {
	Ref<RenderSceneBuffersRD> rd = p_render_data->render_buffers;
	RID env = is_environment(p_render_data->environment) ? p_render_data->environment : RID();

	uint32_t uniform_buffer_index = scene_state.used_uniform_buffer_count;
	++scene_state.used_uniform_buffer_count;

	if (uniform_buffer_index >= scene_state.uniform_buffers.size()) {
		uint32_t from = scene_state.uniform_buffers.size();
		scene_state.uniform_buffers.resize(uniform_buffer_index + 1);
		scene_state.implementation_uniform_buffers.resize(uniform_buffer_index + 1);
		for (uint32_t i = from; i < scene_state.uniform_buffers.size(); i++) {
			scene_state.uniform_buffers[i] = p_render_data->scene_data->create_uniform_buffer();
			scene_state.implementation_uniform_buffers[i] = RD::get_singleton()->uniform_buffer_create(sizeof(SceneState::UBO));
		}
	}

	float luminance_multiplier = rd.is_valid() ? rd->get_luminance_multiplier() : 1.0;

	// Shared scene data (camera, environment, ...).
	p_render_data->scene_data->update_ubo(scene_state.uniform_buffers[uniform_buffer_index], get_debug_draw_mode(), env, RID(), p_render_data->camera_attributes, p_pancake_shadows, p_screen_size, p_viewport_size, p_default_bg_color, luminance_multiplier, p_opaque_render_buffers, false);

	// Implementation UBO.
	scene_state.ubo.cluster_shift = Math::get_shift_from_power_of_2(p_render_data->cluster_size);
	scene_state.ubo.max_cluster_element_count_div_32 = p_render_data->cluster_max_elements / 32;
	{
		uint32_t cluster_screen_width = Math::division_round_up((uint32_t)p_screen_size.width, p_render_data->cluster_size);
		uint32_t cluster_screen_height = Math::division_round_up((uint32_t)p_screen_size.height, p_render_data->cluster_size);
		scene_state.ubo.cluster_type_size = cluster_screen_width * cluster_screen_height * (scene_state.ubo.max_cluster_element_count_div_32 + 32);
		scene_state.ubo.cluster_width = cluster_screen_width;
	}

	scene_state.ubo.ss_effects_flags = 0;
	if (p_opaque_render_buffers && env.is_valid() && get_debug_draw_mode() != RSE::VIEWPORT_DEBUG_DRAW_UNSHADED) {
		scene_state.ubo.ssao_ao_affect = environment_get_ssao_ao_channel_affect(env);
		scene_state.ubo.ssao_light_affect = environment_get_ssao_direct_light_affect(env);
		scene_state.ubo.ss_effects_flags = environment_get_ssao_enabled(env) ? (1 << 0) : 0;
	}

	RD::get_singleton()->buffer_update(scene_state.implementation_uniform_buffers[uniform_buffer_index], 0, sizeof(SceneState::UBO), &scene_state.ubo);

	return uniform_buffer_index;
}

void RenderVsDeferredClustered::SceneState::grow_instance_buffer(RenderListType p_render_list, uint32_t p_req_element_count, bool p_append) {
	if (p_req_element_count > 0) {
		if (instance_buffer[p_render_list].get_size(0u) < p_req_element_count * sizeof(SceneState::InstanceData)) {
			instance_buffer[p_render_list].uninit();
			uint32_t new_size = Math::nearest_power_of_2_templated(MAX(uint64_t(INSTANCE_DATA_BUFFER_MIN_SIZE), p_req_element_count));
			instance_buffer[p_render_list].set_storage_size(0u, new_size * sizeof(SceneState::InstanceData));
			curr_gpu_ptr[p_render_list] = nullptr;
		}

		const bool must_remap = instance_buffer[p_render_list].prepare_for_map(p_append);
		if (must_remap) {
			curr_gpu_ptr[p_render_list] = nullptr;
		}
	}
}

void RenderVsDeferredClustered::_fill_instance_data(RenderListType p_render_list, int *p_render_info, uint32_t p_offset, int32_t p_max_elements, bool p_update_buffer) {
	RenderList *rl = &render_list[p_render_list];
	uint32_t element_total = p_max_elements >= 0 ? uint32_t(p_max_elements) : rl->elements.size();

	rl->element_info.resize(p_offset + element_total);

	// If p_offset == 0, grow_instance_buffer resets and increment the buffer.
	// If this behavior ever changes, _render_shadow_begin may need to change.
	scene_state.grow_instance_buffer(p_render_list, p_offset + element_total, p_offset != 0u);
	if (!scene_state.curr_gpu_ptr[p_render_list] && element_total > 0u) {
		// The old buffer was replaced for another larger one. We must start copying from scratch.
		element_total += p_offset;
		p_offset = 0u;
		scene_state.curr_gpu_ptr[p_render_list] = reinterpret_cast<SceneState::InstanceData *>(scene_state.instance_buffer[p_render_list].map_raw_for_upload(0u));
	}

	if (p_render_info) {
		p_render_info[RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += element_total;
	}

	uint32_t repeats = 0;
	GeometryInstanceSurfaceDataCache *prev_surface = nullptr;
	for (uint32_t i = 0; i < element_total; i++) {
		GeometryInstanceSurfaceDataCache *surface = rl->elements[i + p_offset];
		GeometryInstanceVsDeferredClustered *inst = surface->owner;

		SceneState::InstanceData instance_data;

		RendererRD::MaterialStorage::store_transform_transposed_3x4(inst->transform, instance_data.transform);

#ifdef REAL_T_IS_DOUBLE
		// Split the origin into two components, the float approximation and the missing precision.
		// In the shader we will combine these back together to restore the lost precision.
		RendererRD::MaterialStorage::split_double(inst->transform.origin.x, &instance_data.transform[3], &instance_data.model_precision[0]);
		RendererRD::MaterialStorage::split_double(inst->transform.origin.y, &instance_data.transform[7], &instance_data.model_precision[1]);
		RendererRD::MaterialStorage::split_double(inst->transform.origin.z, &instance_data.transform[11], &instance_data.model_precision[2]);
#endif

		instance_data.flags = inst->flags_cache;
		instance_data.layer_mask = inst->layer_mask;
		instance_data.pad[0] = 0;
		instance_data.pad[1] = 0;

		AABB surface_aabb = AABB(Vector3(0.0, 0.0, 0.0), Vector3(1.0, 1.0, 1.0));
		uint64_t format = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_format(surface->surface);
		Vector4 uv_scale = Vector4(0.0, 0.0, 0.0, 0.0);

		if (format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
			surface_aabb = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_aabb(surface->surface);
			uv_scale = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_uv_scale(surface->surface);
		}

		instance_data.set_compressed_aabb(surface_aabb);
		instance_data.set_uv_scale(uv_scale);

		scene_state.curr_gpu_ptr[p_render_list][i + p_offset] = instance_data;

		const bool cant_repeat = instance_data.flags & INSTANCE_DATA_FLAG_MULTIMESH || inst->mesh_instance.is_valid();

		if (prev_surface != nullptr && !cant_repeat && prev_surface->sort.sort_key1 == surface->sort.sort_key1 && prev_surface->sort.sort_key2 == surface->sort.sort_key2 && inst->mirror == prev_surface->owner->mirror && repeats < RenderElementInfo::MAX_REPEATS) {
			//this element is the same as the previous one, count repeats to draw it using instancing
			repeats++;
		} else {
			if (repeats > 0) {
				for (uint32_t j = 1; j <= repeats; j++) {
					rl->element_info[p_offset + i - j].repeat = j;
				}
			}
			repeats = 1;
			if (p_render_info) {
				p_render_info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
			}
		}

		RenderElementInfo &element_info = rl->element_info[p_offset + i];

		element_info.value = uint32_t(surface->sort.sort_key1 & 0xFF); // lod_index.

		if (cant_repeat) {
			prev_surface = nullptr;
		} else {
			prev_surface = surface;
		}
	}

	if (repeats > 0) {
		for (uint32_t j = 1; j <= repeats; j++) {
			rl->element_info[p_offset + element_total - j].repeat = j;
		}
	}

	if (p_update_buffer && element_total > 0u) {
		RenderingDevice::get_singleton()->buffer_flush(scene_state.instance_buffer[p_render_list]._get(0u));
	}
}

_FORCE_INLINE_ static uint32_t _indices_to_primitives(RSE::PrimitiveType p_primitive, uint32_t p_indices) {
	static const uint32_t divisor[RSE::PRIMITIVE_MAX] = { 1, 2, 1, 3, 1 };
	static const uint32_t subtractor[RSE::PRIMITIVE_MAX] = { 0, 0, 1, 0, 2 };
	return (p_indices - subtractor[p_primitive]) / divisor[p_primitive];
}

void RenderVsDeferredClustered::_fill_render_list(RenderListType p_render_list, const RenderDataRD *p_render_data, PassMode p_pass_mode, bool p_append) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();

	Plane near_plane = Plane(-p_render_data->scene_data->cam_transform.basis.get_column(Vector3::AXIS_Z), p_render_data->scene_data->cam_transform.origin);
	near_plane.d += p_render_data->scene_data->cam_projection.get_z_near();
	float z_max = p_render_data->scene_data->cam_projection.get_z_far() - p_render_data->scene_data->cam_projection.get_z_near();

	RenderList *rl = &render_list[p_render_list];
	_update_dirty_geometry_instances();

	if (!p_append) {
		rl->clear();
		if (p_render_list == RENDER_LIST_OPAQUE) {
			// Opaque fills the alpha list.
			render_list[RENDER_LIST_ALPHA].clear();
		}
	}

	const int info_type = p_render_list == RENDER_LIST_SECONDARY ? RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW : RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE;

	//fill list

	for (int i = 0; i < (int)p_render_data->instances->size(); i++) {
		GeometryInstanceVsDeferredClustered *inst = static_cast<GeometryInstanceVsDeferredClustered *>((*p_render_data->instances)[i]);

		Vector3 center = inst->transform.origin;
		if (p_render_data->scene_data->cam_orthogonal) {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.get_support(-near_plane.normal);
			}
			inst->depth = near_plane.distance_to(center) - inst->sorting_offset;
		} else {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.position + (inst->transformed_aabb.size * 0.5);
			}
			inst->depth = p_render_data->scene_data->cam_transform.origin.distance_to(center) - inst->sorting_offset;
		}
		uint32_t depth_layer = CLAMP(int(inst->depth * 16 / z_max), 0, 15);

		uint32_t flags = inst->base_flags; //fill flags if appropriate

		if (inst->non_uniform_scale) {
			flags |= INSTANCE_DATA_FLAGS_NON_UNIFORM_SCALE;
		}

		float fade_alpha = 1.0;
		if (inst->fade_near || inst->fade_far) {
			float fade_dist = inst->transformed_aabb.get_center().distance_to(p_render_data->scene_data->cam_transform.origin);
			// Use `smoothstep()` to make opacity changes more gradual and less noticeable to the player.
			if (inst->fade_far && fade_dist > inst->fade_far_begin) {
				fade_alpha = Math::smoothstep(0.0f, 1.0f, 1.0f - (fade_dist - inst->fade_far_begin) / (inst->fade_far_end - inst->fade_far_begin));
			} else if (inst->fade_near && fade_dist < inst->fade_near_end) {
				fade_alpha = Math::smoothstep(0.0f, 1.0f, (fade_dist - inst->fade_near_begin) / (inst->fade_near_end - inst->fade_near_begin));
			}
		}

		fade_alpha *= inst->force_alpha * inst->parent_fade_alpha;

		flags = (flags & ~INSTANCE_DATA_FLAGS_FADE_MASK) | (uint32_t(fade_alpha * 255.0) << INSTANCE_DATA_FLAGS_FADE_SHIFT);

		inst->flags_cache = flags;

		float lod_distance = 0.0;

		if (p_render_data->scene_data->cam_orthogonal) {
			lod_distance = 1.0;
		} else {
			Vector3 aabb_min = inst->transformed_aabb.position;
			Vector3 aabb_max = inst->transformed_aabb.position + inst->transformed_aabb.size;
			Vector3 camera_position = p_render_data->scene_data->main_cam_transform.origin;
			Vector3 surface_distance = Vector3(0.0, 0.0, 0.0).max(aabb_min - camera_position).max(camera_position - aabb_max);

			lod_distance = surface_distance.length();
		}

		GeometryInstanceSurfaceDataCache *surf = inst->surface_caches;

		while (surf) {
			// LOD
			uint32_t primitives = 0;
			if (p_render_data->scene_data->screen_mesh_lod_threshold > 0.0 && mesh_storage->mesh_surface_has_lod(surf->surface)) {
				uint32_t indices = 0;
				surf->sort.lod_index = mesh_storage->mesh_surface_get_lod(surf->surface, inst->lod_model_scale * inst->lod_bias, lod_distance * p_render_data->scene_data->lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, indices);
				primitives = _indices_to_primitives(surf->primitive, indices);
			} else {
				surf->sort.lod_index = 0;
				// This does not include primitives rendered via indirect draw calls.
				primitives = _indices_to_primitives(surf->primitive, mesh_storage->mesh_surface_get_vertices_drawn_count(surf->surface)) * inst->instance_count;
			}
			if (p_render_data->render_info) {
				p_render_data->render_info->info[info_type][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += primitives;
			}

			// ADD Element
			if (p_pass_mode == PASS_MODE_COLOR) {
				bool force_alpha = fade_alpha < FADE_ALPHA_PASS_THRESHOLD;

				if (!force_alpha && (surf->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE))) {
					rl->add_element(surf);
				}

				if (force_alpha || (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA)) {
					surf->color_pass_inclusion_mask = COLOR_PASS_FLAG_TRANSPARENT;
					render_list[RENDER_LIST_ALPHA].add_element(surf);
				} else {
					surf->color_pass_inclusion_mask = 0;
				}
			} else if (p_pass_mode == PASS_MODE_SHADOW || p_pass_mode == PASS_MODE_SHADOW_DP) {
				if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW) {
					rl->add_element(surf);
				}
			} else {
				if (surf->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE)) {
					rl->add_element(surf);
				}
			}

			surf->sort.depth_layer = depth_layer;

			surf = surf->next;
		}
	}
}

/* Lighting */

void RenderVsDeferredClustered::setup_added_light(const RSE::LightType p_type, const Transform3D &p_transform, float p_radius, float p_spot_aperture, const Vector2 &p_area_size) {
	// Area lights are not supported, they are left out of the clusters.
	if (current_cluster_builder != nullptr && p_type != RSE::LIGHT_AREA) {
		ClusterBuilderRD::LightType type = p_type == RSE::LIGHT_SPOT ? ClusterBuilderRD::LIGHT_TYPE_SPOT : ClusterBuilderRD::LIGHT_TYPE_OMNI;
		current_cluster_builder->add_light(type, p_transform, p_radius, p_spot_aperture, p_area_size);
	}
}

/* Render scene */

void RenderVsDeferredClustered::_render_shadows(RenderDataRD *p_render_data) {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	RENDER_TIMESTAMP("Render Shadows");

	p_render_data->cube_shadows.clear();
	p_render_data->shadows.clear();
	p_render_data->directional_shadows.clear();

	for (int i = 0; i < p_render_data->render_shadow_count; i++) {
		RID li = p_render_data->render_shadows[i].light;
		RID base = light_storage->light_instance_get_base_light(li);

		if (light_storage->light_get_type(base) == RSE::LIGHT_DIRECTIONAL) {
			p_render_data->directional_shadows.push_back(i);
		} else if (light_storage->light_get_type(base) == RSE::LIGHT_AREA) {
			continue; // Area lights are not supported.
		} else if (light_storage->light_get_type(base) == RSE::LIGHT_OMNI && light_storage->light_omni_get_shadow_mode(base) == RSE::LIGHT_OMNI_SHADOW_CUBE) {
			p_render_data->cube_shadows.push_back(i);
		} else {
			p_render_data->shadows.push_back(i);
		}
	}

	const float lod_distance_multiplier = p_render_data->scene_data->cam_projection.get_lod_multiplier();
	const float lod_threshold = p_render_data->scene_data->screen_mesh_lod_threshold;
	const Transform3D &cam_transform = p_render_data->scene_data->cam_transform;

	// Cube shadows are rendered in their own way (into a cubemap, then copied into the atlas as dual paraboloid).
	for (const int &index : p_render_data->cube_shadows) {
		const RenderShadowData &shadow = p_render_data->render_shadows[index];
		_render_shadow_pass(shadow.light, p_render_data->shadow_atlas, shadow.pass, shadow.instances, lod_distance_multiplier, lod_threshold, true, true, p_render_data->render_info, cam_transform);
	}

	if (p_render_data->directional_shadows.size()) {
		// Clear the directional shadow atlas.
		light_storage->update_directional_shadow_atlas();
		RD::get_singleton()->draw_list_begin(light_storage->direction_shadow_get_fb(), RD::DRAW_CLEAR_DEPTH, Vector<Color>(), 0.0f);
		RD::get_singleton()->draw_list_end();
	}

	if (p_render_data->directional_shadows.is_empty() && p_render_data->shadows.is_empty()) {
		return;
	}

	_render_shadow_begin();

	for (uint32_t i = 0; i < p_render_data->directional_shadows.size(); i++) {
		const RenderShadowData &shadow = p_render_data->render_shadows[p_render_data->directional_shadows[i]];
		_render_shadow_pass(shadow.light, p_render_data->shadow_atlas, shadow.pass, shadow.instances, lod_distance_multiplier, lod_threshold, false, false, p_render_data->render_info, cam_transform);
	}
	for (uint32_t i = 0; i < p_render_data->shadows.size(); i++) {
		const RenderShadowData &shadow = p_render_data->render_shadows[p_render_data->shadows[i]];
		_render_shadow_pass(shadow.light, p_render_data->shadow_atlas, shadow.pass, shadow.instances, lod_distance_multiplier, lod_threshold, i == 0, true, p_render_data->render_info, cam_transform);
	}

	_render_shadow_process();
	_render_shadow_end();
}

void RenderVsDeferredClustered::_process_ssao(RenderDataRD *p_render_data, RID p_normal_roughness) {
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	Ref<RenderBufferDataVsDeferredClustered> rb_data = rb->get_custom_data(RB_SCOPE_VS_DEFERRED_CLUSTERED);
	RID env = p_render_data->environment;

	RENDER_TIMESTAMP("Process SSAO");
	RD::get_singleton()->draw_command_begin_label("SSAO");

	// Convert the depth buffer into linear, downsampled depth.
	ss_effects->downsample_depth(rb, 0, p_render_data->scene_data->view_projection[0]);

	RendererRD::SSEffects::SSAOSettings settings;
	settings.radius = environment_get_ssao_radius(env);
	settings.intensity = environment_get_ssao_intensity(env);
	settings.power = environment_get_ssao_power(env);
	settings.detail = environment_get_ssao_detail(env);
	settings.horizon = environment_get_ssao_horizon(env);
	settings.sharpness = environment_get_ssao_sharpness(env);
	settings.full_screen_size = rb->get_internal_size();

	ss_effects->ssao_allocate_buffers(rb, rb_data->ssao, settings);
	ss_effects->generate_ssao(rb, rb_data->ssao, 0, p_normal_roughness, p_render_data->scene_data->view_projection[0], settings);

	RD::get_singleton()->draw_command_end_label();
}

void RenderVsDeferredClustered::_setup_lights(RenderDataRD *p_render_data) {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	RENDER_TIMESTAMP("Setup Lights");

	// Lights are added to the cluster builder through setup_added_light() while the light buffers are filled.
	current_cluster_builder->begin(p_render_data->scene_data->cam_transform, p_render_data->scene_data->cam_projection, true);

	uint32_t directional_light_count = 0;
	uint32_t positional_light_count = 0;
	light_storage->update_light_buffers(p_render_data, *p_render_data->lights, p_render_data->scene_data->cam_transform, p_render_data->shadow_atlas, true, directional_light_count, positional_light_count, p_render_data->directional_light_soft_shadows);
	p_render_data->directional_light_count = directional_light_count;

	current_cluster_builder->bake_cluster();
}

void RenderVsDeferredClustered::_render_scene(RenderDataRD *p_render_data, const Color &p_default_bg_color) {
	ERR_FAIL_NULL(p_render_data);

	if (p_render_data->reflection_probe.is_valid()) {
		return; // Dummy: reflection probe rendering is not supported (probes stay empty and are ignored by shading).
	}

	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	ERR_FAIL_COND(rb.is_null());
	Ref<RenderBufferDataVsDeferredClustered> rb_data = rb->get_custom_data(RB_SCOPE_VS_DEFERRED_CLUSTERED);
	ERR_FAIL_COND(rb_data.is_null());

	RENDER_TIMESTAMP("Prepare 3D Scene");

	scene_state.used_uniform_buffer_count = 0;

	current_cluster_builder = rb_data->cluster_builder;
	p_render_data->cluster_buffer = current_cluster_builder->get_cluster_buffer();
	p_render_data->cluster_size = current_cluster_builder->get_cluster_size();
	p_render_data->cluster_max_elements = current_cluster_builder->get_max_cluster_elements();

	p_render_data->scene_data->calculate_motion_vectors = false;
	p_render_data->scene_data->directional_light_count = 0;
	p_render_data->scene_data->opaque_prepass_threshold = 0.99f;
	p_render_data->scene_data->emissive_exposure_normalization = -1.0;

	const Size2i screen_size = rb->get_internal_size();
	const RendererRD::MaterialStorage::Samplers samplers = rb->get_samplers();
	const bool reverse_cull = p_render_data->scene_data->cam_transform.basis.determinant() < 0;
	const RID env = p_render_data->environment;
	const bool using_ssao = env.is_valid() && environment_get_ssao_enabled(env);
	int *render_info = p_render_data->render_info ? p_render_data->render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE] : (int *)nullptr;

	/* Setup: fill and sort the render lists */

	RD::get_singleton()->draw_command_begin_label("Render Setup");

	uint32_t depth_prepass_uniform_buffer_index = _setup_environment(p_render_data, screen_size, screen_size, p_default_bg_color, false);

	// May have changed due to the above (light buffer enlarged, as an example).
	_update_render_base_uniform_set();

	_fill_render_list(RENDER_LIST_OPAQUE, p_render_data, PASS_MODE_COLOR);
	render_list[RENDER_LIST_OPAQUE].sort_by_key();
	render_list[RENDER_LIST_ALPHA].sort_by_reverse_depth_and_priority();
	_fill_instance_data(RENDER_LIST_OPAQUE, render_info);
	_fill_instance_data(RENDER_LIST_ALPHA, render_info);

	RD::get_singleton()->draw_command_end_label();

	/* Setup: background and sky (radiance is used for ambient and reflections) */

	RID radiance_texture;
	bool draw_sky = false;
	// We invert luminance_multiplier for sky so that we can combine it with exposure value.
	float sky_luminance_multiplier = 1.0 / rb->get_luminance_multiplier();
	float sky_brightness_multiplier = 1.0;

	Color clear_color;
	bool load_color = false;

	if (is_environment(env)) {
		RSE::EnvironmentBG bg_mode = environment_get_background(env);
		float bg_energy_multiplier = environment_get_bg_energy_multiplier(env) * environment_get_bg_intensity(env);
		RSE::EnvironmentReflectionSource reflection_source = environment_get_reflection_source(env);

		if (p_render_data->camera_attributes.is_valid()) {
			bg_energy_multiplier *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
		}

		switch (bg_mode) {
			case RSE::ENV_BG_CLEAR_COLOR:
			case RSE::ENV_BG_COLOR: {
				clear_color = bg_mode == RSE::ENV_BG_COLOR ? environment_get_bg_color(env) : p_default_bg_color;
				clear_color.r *= bg_energy_multiplier;
				clear_color.g *= bg_energy_multiplier;
				clear_color.b *= bg_energy_multiplier;
			} break;
			case RSE::ENV_BG_SKY: {
				draw_sky = !p_render_data->transparent_bg;
			} break;
			case RSE::ENV_BG_CANVAS: // Canvas background is not supported, behaves like "keep".
			case RSE::ENV_BG_KEEP: {
				load_color = true;
			} break;
			default: {
			}
		}

		// Setup sky if used for ambient, reflections, or background.
		if (draw_sky || (reflection_source == RSE::ENV_REFLECTION_SOURCE_BG && bg_mode == RSE::ENV_BG_SKY) || reflection_source == RSE::ENV_REFLECTION_SOURCE_SKY || environment_get_ambient_source(env) == RSE::ENV_AMBIENT_SOURCE_SKY) {
			RENDER_TIMESTAMP("Setup Sky");
			RD::get_singleton()->draw_command_begin_label("Setup Sky");

			sky.setup_sky(p_render_data, screen_size);

			sky_brightness_multiplier *= bg_energy_multiplier;

			RID sky_rid = environment_get_sky(env);
			if (sky_rid.is_valid()) {
				sky.update_radiance_buffers(rb, env, p_render_data->scene_data->cam_transform.origin, time, sky_luminance_multiplier, sky_brightness_multiplier);
				radiance_texture = sky.sky_get_radiance_texture_rd(sky_rid);
			} else {
				// do not try to draw sky if invalid
				draw_sky = false;
			}

			if (draw_sky) {
				// update sky half/quarter res buffers (if required)
				sky.update_res_buffers(rb, env, time, sky_luminance_multiplier, sky_brightness_multiplier);
			}

			RD::get_singleton()->draw_command_end_label();
		}
	} else {
		clear_color = p_default_bg_color;
	}

	/* 1. Depth pre-pass (+ normal/roughness when SSAO needs it) */
	{
		RENDER_TIMESTAMP("Render Depth Pre-Pass");
		RD::get_singleton()->draw_command_begin_label("Render Depth Pre-Pass");

		Vector<Color> depth_pass_clear;
		if (using_ssao) {
			depth_pass_clear.push_back(Color(0, 0, 0, 0));
		}

		RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_OPAQUE, nullptr, RID(), samplers, depth_prepass_uniform_buffer_index);
		RenderList &rl = render_list[RENDER_LIST_OPAQUE];
		RenderListParameters render_list_params(rl.elements.ptr(), rl.element_info.ptr(), rl.elements.size(), reverse_cull, using_ssao ? PASS_MODE_DEPTH_NORMAL_ROUGHNESS : PASS_MODE_DEPTH, 0, rp_uniform_set);
		_render_list_with_draw_list(&render_list_params, rb_data->get_depth_fb(using_ssao), RD::DRAW_CLEAR_ALL, depth_pass_clear, 0.0f, p_render_data->render_region);

		RD::get_singleton()->draw_command_end_label();
	}

	/* 2. Shadow passes */
	_render_shadows(p_render_data);

	/* 3. SSAO (compute) */
	if (using_ssao) {
		_process_ssao(p_render_data, rb->get_texture_slice(RB_SCOPE_VS_DEFERRED_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS, 0, 0));
	}

	/* 4. Lights and clusters */
	_setup_lights(p_render_data);

	p_render_data->scene_data->directional_light_count = p_render_data->directional_light_count;
	p_render_data->scene_data->opaque_prepass_threshold = 0.0f;

	// Shadow passes and light setup can change the base uniform set.
	_update_render_base_uniform_set();

	RID color_framebuffer = rb_data->get_color_fb();

	/* 5. Opaque color pass */
	{
		RENDER_TIMESTAMP("Render Opaque Pass");
		RD::get_singleton()->draw_command_begin_label("Render Opaque Pass");

		uint32_t opaque_pass_uniform_buffer_index = _setup_environment(p_render_data, screen_size, screen_size, p_default_bg_color, true);
		RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_OPAQUE, p_render_data, radiance_texture, samplers, opaque_pass_uniform_buffer_index, true);

		Vector<Color> c;
		if (!load_color) {
			Color cc = clear_color.srgb_to_linear();
			cc.a = 0; // Same as Forward+.
			c.push_back(cc);
		}

		RenderList &rl = render_list[RENDER_LIST_OPAQUE];
		RenderListParameters render_list_params(rl.elements.ptr(), rl.element_info.ptr(), rl.elements.size(), reverse_cull, PASS_MODE_COLOR, 0, rp_uniform_set);
		_render_list_with_draw_list(&render_list_params, color_framebuffer, load_color ? RD::DRAW_DEFAULT_ALL : RD::DRAW_CLEAR_COLOR_ALL, c, 0.0f, p_render_data->render_region);

		RD::get_singleton()->draw_command_end_label();
	}

	/* 6. Sky */
	if (draw_sky) {
		RENDER_TIMESTAMP("Render Sky");
		RD::get_singleton()->draw_command_begin_label("Draw Sky");

		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(color_framebuffer, RD::DRAW_DEFAULT_ALL, Vector<Color>(), 1.0f, 0u, p_render_data->render_region);
		sky.draw_sky(draw_list, rb, env, color_framebuffer, time, sky_luminance_multiplier, sky_brightness_multiplier);
		RD::get_singleton()->draw_list_end();

		RD::get_singleton()->draw_command_end_label();
	}

	/* 7. Transparent color pass (sorted back to front, blended, no depth write) */
	{
		RENDER_TIMESTAMP("Render 3D Transparent Pass");
		RD::get_singleton()->draw_command_begin_label("Render 3D Transparent Pass");

		uint32_t transparent_pass_uniform_buffer_index = _setup_environment(p_render_data, screen_size, screen_size, p_default_bg_color, false);
		RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_ALPHA, p_render_data, radiance_texture, samplers, transparent_pass_uniform_buffer_index, true);

		RenderList &rl = render_list[RENDER_LIST_ALPHA];
		RenderListParameters render_list_params(rl.elements.ptr(), rl.element_info.ptr(), rl.elements.size(), reverse_cull, PASS_MODE_COLOR, COLOR_PASS_FLAG_TRANSPARENT, rp_uniform_set);
		_render_list_with_draw_list(&render_list_params, color_framebuffer, RD::DRAW_DEFAULT_ALL, Vector<Color>(), 0.0f, p_render_data->render_region);

		RD::get_singleton()->draw_command_end_label();
	}

	/* 8. Tonemap / post-process into the render target (shared code) */
	RENDER_TIMESTAMP("Tonemap");
	_render_buffers_post_process_and_tonemap(p_render_data);
}

/* Shadows */

void RenderVsDeferredClustered::_render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, bool p_open_pass, bool p_clear_region, RenderingServerTypes::RenderInfo *p_render_info, const Transform3D &p_main_cam_transform) {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	ERR_FAIL_COND(!light_storage->owns_light_instance(p_light));

	RID base = light_storage->light_instance_get_base_light(p_light);

	Rect2i atlas_rect;
	uint32_t atlas_size = 1;
	RID atlas_fb;

	bool reverse_cull_face = light_storage->light_get_reverse_cull_face_mode(base);
	bool using_dual_paraboloid = false;
	bool using_dual_paraboloid_flip = false;
	Vector2i dual_paraboloid_offset;
	RID render_fb;
	RID render_texture;
	float zfar;

	bool use_pancake = false;
	bool render_cubemap = false;
	bool finalize_cubemap = false;

	bool flip_y = false;

	Projection light_projection;
	Transform3D light_transform;

	if (light_storage->light_get_type(base) == RSE::LIGHT_DIRECTIONAL) {
		//set pssm stuff
		uint64_t last_scene_shadow_pass = light_storage->light_instance_get_shadow_pass(p_light);
		if (last_scene_shadow_pass != get_scene_pass()) {
			light_storage->light_instance_set_directional_rect(p_light, light_storage->get_directional_shadow_rect());
			light_storage->directional_shadow_increase_current_light();
			light_storage->light_instance_set_shadow_pass(p_light, get_scene_pass());
		}

		use_pancake = light_storage->light_get_param(base, RSE::LIGHT_PARAM_SHADOW_PANCAKE_SIZE) > 0;
		light_projection = light_storage->light_instance_get_shadow_camera(p_light, p_pass);
		light_transform = light_storage->light_instance_get_shadow_transform(p_light, p_pass);

		atlas_rect = light_storage->light_instance_get_directional_rect(p_light);

		if (light_storage->light_directional_get_shadow_mode(base) == RSE::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_4_SPLITS) {
			atlas_rect.size.width /= 2;
			atlas_rect.size.height /= 2;

			if (p_pass == 1) {
				atlas_rect.position.x += atlas_rect.size.width;
			} else if (p_pass == 2) {
				atlas_rect.position.y += atlas_rect.size.height;
			} else if (p_pass == 3) {
				atlas_rect.position += atlas_rect.size;
			}
		} else if (light_storage->light_directional_get_shadow_mode(base) == RSE::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS) {
			atlas_rect.size.height /= 2;

			if (p_pass != 0) {
				atlas_rect.position.y += atlas_rect.size.height;
			}
		}

		float directional_shadow_size = light_storage->directional_shadow_get_size();
		Rect2 atlas_rect_norm = atlas_rect;
		atlas_rect_norm.position /= directional_shadow_size;
		atlas_rect_norm.size /= directional_shadow_size;
		light_storage->light_instance_set_directional_shadow_atlas_rect(p_light, p_pass, atlas_rect_norm);

		zfar = RSG::light_storage->light_get_param(base, RSE::LIGHT_PARAM_RANGE);

		render_fb = light_storage->direction_shadow_get_fb();
		render_texture = RID();
		flip_y = true;

	} else {
		//set from shadow atlas

		ERR_FAIL_COND(!light_storage->owns_shadow_atlas(p_shadow_atlas));
		ERR_FAIL_COND(!light_storage->shadow_atlas_owns_light_instance(p_shadow_atlas, p_light));

		RSG::light_storage->shadow_atlas_update(p_shadow_atlas);

		uint32_t key = light_storage->shadow_atlas_get_light_instance_key(p_shadow_atlas, p_light);

		uint32_t quadrant = (key >> RendererRD::LightStorage::QUADRANT_SHIFT) & 0x3;
		uint32_t shadow = key & RendererRD::LightStorage::SHADOW_INDEX_MASK;
		uint32_t subdivision = light_storage->shadow_atlas_get_quadrant_subdivision(p_shadow_atlas, quadrant);

		ERR_FAIL_INDEX((int)shadow, light_storage->shadow_atlas_get_quadrant_shadow_size(p_shadow_atlas, quadrant));

		uint32_t shadow_atlas_size = light_storage->shadow_atlas_get_size(p_shadow_atlas);
		uint32_t quadrant_size = shadow_atlas_size >> 1;

		atlas_rect.position.x = (quadrant & 1) * quadrant_size;
		atlas_rect.position.y = (quadrant >> 1) * quadrant_size;

		uint32_t shadow_size = (quadrant_size / subdivision);
		atlas_rect.position.x += (shadow % subdivision) * shadow_size;
		atlas_rect.position.y += (shadow / subdivision) * shadow_size;

		atlas_rect.size.width = shadow_size;
		atlas_rect.size.height = shadow_size;

		zfar = light_storage->light_get_param(base, RSE::LIGHT_PARAM_RANGE);

		if (light_storage->light_get_type(base) == RSE::LIGHT_OMNI) {
			bool wrap = (shadow + 1) % subdivision == 0;
			dual_paraboloid_offset = wrap ? Vector2i(1 - subdivision, 1) : Vector2i(1, 0);

			if (light_storage->light_omni_get_shadow_mode(base) == RSE::LIGHT_OMNI_SHADOW_CUBE) {
				render_texture = light_storage->get_cubemap(shadow_size / 2);
				render_fb = light_storage->get_cubemap_fb(shadow_size / 2, p_pass);

				light_projection = light_storage->light_instance_get_shadow_camera(p_light, p_pass);
				light_transform = light_storage->light_instance_get_shadow_transform(p_light, p_pass);
				render_cubemap = true;
				finalize_cubemap = p_pass == 5;
				atlas_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);

				atlas_size = shadow_atlas_size;

				if (p_pass == 0) {
					_render_shadow_begin();
				}

			} else {
				atlas_rect.position.x += 1;
				atlas_rect.position.y += 1;
				atlas_rect.size.x -= 2;
				atlas_rect.size.y -= 2;

				atlas_rect.position += p_pass * atlas_rect.size * dual_paraboloid_offset;

				light_projection = light_storage->light_instance_get_shadow_camera(p_light, 0);
				light_transform = light_storage->light_instance_get_shadow_transform(p_light, 0);

				using_dual_paraboloid = true;
				using_dual_paraboloid_flip = p_pass == 1;
				render_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);
				flip_y = true;
			}

		} else if (light_storage->light_get_type(base) == RSE::LIGHT_SPOT) {
			light_projection = light_storage->light_instance_get_shadow_camera(p_light, 0);
			light_transform = light_storage->light_instance_get_shadow_transform(p_light, 0);

			render_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);

			flip_y = true;
		}
	}

	if (render_cubemap) {
		//rendering to cubemap
		_render_shadow_append(render_fb, p_instances, light_projection, light_transform, zfar, reverse_cull_face, false, false, use_pancake, p_lod_distance_multiplier, p_screen_mesh_lod_threshold, Rect2(), false, true, true, p_render_info, p_main_cam_transform);
		if (finalize_cubemap) {
			_render_shadow_process();
			_render_shadow_end();
			//reblit
			Rect2 atlas_rect_norm = atlas_rect;
			atlas_rect_norm.position /= float(atlas_size);
			atlas_rect_norm.size /= float(atlas_size);
			copy_effects->copy_cubemap_to_dp(render_texture, atlas_fb, atlas_rect_norm, atlas_rect.size, light_projection.get_z_near(), zfar, false);
			atlas_rect_norm.position += Vector2(dual_paraboloid_offset) * atlas_rect_norm.size;
			copy_effects->copy_cubemap_to_dp(render_texture, atlas_fb, atlas_rect_norm, atlas_rect.size, light_projection.get_z_near(), zfar, true);

			//restore transform so it can be properly used
			light_storage->light_instance_set_shadow_transform(p_light, Projection(), light_storage->light_instance_get_base_transform(p_light), zfar, 0, 0, 0);
		}

	} else {
		//render shadow
		_render_shadow_append(render_fb, p_instances, light_projection, light_transform, zfar, reverse_cull_face, using_dual_paraboloid, using_dual_paraboloid_flip, use_pancake, p_lod_distance_multiplier, p_screen_mesh_lod_threshold, atlas_rect, flip_y, p_clear_region, p_open_pass, p_render_info, p_main_cam_transform);
	}
}

void RenderVsDeferredClustered::_render_shadow_begin() {
	scene_state.shadow_passes.clear();
	RD::get_singleton()->draw_command_begin_label("Shadow Setup");
	_update_render_base_uniform_set();

	render_list[RENDER_LIST_SECONDARY].clear();
	// No need to reset scene_state.curr_gpu_ptr or scene_state.instance_buffer[RENDER_LIST_SECONDARY]
	// because _fill_instance_data will do that if it detects p_offset == 0u.
}

void RenderVsDeferredClustered::_render_shadow_append(RID p_framebuffer, const PagedArray<RenderGeometryInstance *> &p_instances, const Projection &p_projection, const Transform3D &p_transform, float p_zfar, bool p_reverse_cull_face, bool p_use_dp, bool p_use_dp_flip, bool p_use_pancake, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, const Rect2i &p_rect, bool p_flip_y, bool p_clear_region, bool p_begin, RenderingServerTypes::RenderInfo *p_render_info, const Transform3D &p_main_cam_transform) {
	RenderSceneDataRD scene_data;
	scene_data.flip_y = !p_flip_y; // Q: Why is this inverted? Do we assume flip in shadow logic?
	scene_data.cam_projection = p_projection;
	scene_data.cam_transform = p_transform;
	scene_data.view_projection[0] = p_projection;
	scene_data.z_far = p_zfar;
	scene_data.z_near = 0.0;
	scene_data.lod_distance_multiplier = p_lod_distance_multiplier;
	scene_data.dual_paraboloid_side = p_use_dp_flip ? -1 : 1;
	scene_data.opaque_prepass_threshold = 0.1f;
	scene_data.time = time;
	scene_data.time_step = time_step;
	scene_data.main_cam_transform = p_main_cam_transform;
	scene_data.shadow_pass = true;
	scene_data.screen_mesh_lod_threshold = get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_DISABLE_LOD ? 0.0 : p_screen_mesh_lod_threshold;

	RenderDataRD render_data;
	render_data.scene_data = &scene_data;
	render_data.cluster_size = 1;
	render_data.cluster_max_elements = 32;
	render_data.instances = &p_instances;
	render_data.render_info = p_render_info;

	Size2i screen_size = RD::get_singleton()->framebuffer_get_size(p_framebuffer);
	Size2i viewport_size = p_rect.size;
	if (viewport_size == Size2()) {
		viewport_size = screen_size;
	}
	uint32_t uniform_buffer_index = _setup_environment(&render_data, screen_size, viewport_size, Color(), false, p_use_pancake);

	PassMode pass_mode = p_use_dp ? PASS_MODE_SHADOW_DP : PASS_MODE_SHADOW;

	uint32_t render_list_from = render_list[RENDER_LIST_SECONDARY].elements.size();
	_fill_render_list(RENDER_LIST_SECONDARY, &render_data, pass_mode, true);
	uint32_t render_list_size = render_list[RENDER_LIST_SECONDARY].elements.size() - render_list_from;
	render_list[RENDER_LIST_SECONDARY].sort_by_key_range(render_list_from, render_list_size);
	_fill_instance_data(RENDER_LIST_SECONDARY, p_render_info ? p_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW] : (int *)nullptr, render_list_from, render_list_size, false);

	SceneState::ShadowPass shadow_pass;
	shadow_pass.element_from = render_list_from;
	shadow_pass.element_count = render_list_size;
	shadow_pass.flip_cull = p_use_dp_flip != p_flip_y != p_reverse_cull_face;
	shadow_pass.pass_mode = pass_mode;
	shadow_pass.rp_uniform_set = RID(); //will be filled later when instance buffer is complete
	shadow_pass.framebuffer = p_framebuffer;
	shadow_pass.clear_depth = p_begin || p_clear_region;
	shadow_pass.rect = p_rect;
	shadow_pass.uniform_buffer_index = uniform_buffer_index;

	scene_state.shadow_passes.push_back(shadow_pass);
}

void RenderVsDeferredClustered::_render_shadow_process() {
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (scene_state.instance_buffer[RENDER_LIST_SECONDARY].get_size(0u) > 0u) {
		rd->buffer_flush(scene_state.instance_buffer[RENDER_LIST_SECONDARY]._get(0u));
	}

	for (SceneState::ShadowPass &shadow_pass : scene_state.shadow_passes) {
		//render passes need to be configured after instance buffer is done, since they need the latest version
		shadow_pass.rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_SECONDARY, nullptr, RID(), RendererRD::MaterialStorage::get_singleton()->samplers_rd_get_default(), shadow_pass.uniform_buffer_index, false);
	}

	RD::get_singleton()->draw_command_end_label();
}

void RenderVsDeferredClustered::_render_shadow_end() {
	RD::get_singleton()->draw_command_begin_label("Shadow Render");

	for (SceneState::ShadowPass &shadow_pass : scene_state.shadow_passes) {
		RenderListParameters render_list_parameters(render_list[RENDER_LIST_SECONDARY].elements.ptr() + shadow_pass.element_from, render_list[RENDER_LIST_SECONDARY].element_info.ptr() + shadow_pass.element_from, shadow_pass.element_count, shadow_pass.flip_cull, shadow_pass.pass_mode, 0, shadow_pass.rp_uniform_set, shadow_pass.element_from);
		_render_list_with_draw_list(&render_list_parameters, shadow_pass.framebuffer, shadow_pass.clear_depth ? RD::DRAW_CLEAR_DEPTH : RD::DRAW_DEFAULT_ALL, Vector<Color>(), 0.0f, shadow_pass.rect);
	}

	RD::get_singleton()->draw_command_end_label();
}

/* Uniform sets */

void RenderVsDeferredClustered::base_uniforms_changed() {
	if (!render_base_uniform_set.is_null() && RD::get_singleton()->uniform_set_is_valid(render_base_uniform_set)) {
		RD::get_singleton()->free_rid(render_base_uniform_set);
	}
	render_base_uniform_set = RID();
}

void RenderVsDeferredClustered::_update_render_base_uniform_set() {
	if (render_base_uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(render_base_uniform_set)) {
		return;
	}

	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();

	Vector<RD::Uniform> uniforms;
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 0, scene_shader.shadow_sampler));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, light_storage->get_omni_light_buffer()));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, light_storage->get_spot_light_buffer()));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 3, light_storage->get_directional_light_buffer()));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 4, material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED)));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, best_fit_normal.texture));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, dfg_lut.texture));

	render_base_uniform_set = RD::get_singleton()->uniform_set_create(uniforms, scene_shader.default_shader_rd, SCENE_UNIFORM_SET);
}

RID RenderVsDeferredClustered::_setup_render_pass_uniform_set(RenderListType p_render_list, const RenderDataRD *p_render_data, RID p_radiance_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index, bool p_use_directional_shadow_atlas) {
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	// Render buffers are only passed for the opaque/transparent color passes.
	Ref<RenderSceneBuffersRD> rb;
	if (p_render_data && p_render_data->render_buffers.is_valid()) {
		rb = p_render_data->render_buffers;
	}

	thread_local LocalVector<RD::Uniform> uniforms;
	uniforms.clear();

	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, scene_state.uniform_buffers[p_uniform_buffer_index]));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 1, scene_state.implementation_uniform_buffers[p_uniform_buffer_index]));

	{
		if (scene_state.instance_buffer[p_render_list].get_size(0u) == 0u) {
			// Any buffer will do since it's not used, so just create one.
			// We can't use scene_shader.default_vec4_xform_buffer because it's not dynamic.
			scene_state.instance_buffer[p_render_list].set_storage_size(0u, INSTANCE_DATA_BUFFER_MIN_SIZE * sizeof(SceneState::InstanceData));
			scene_state.instance_buffer[p_render_list].prepare_for_upload();
		}
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC, 2, scene_state.instance_buffer[p_render_list]._get(0u)));
	}

	{
		RID radiance_texture = p_radiance_texture;
		if (radiance_texture.is_null()) {
			radiance_texture = texture_storage->texture_rd_get_default(is_using_radiance_octmap_array() ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		}
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, radiance_texture));
	}

	{
		RID texture;
		if (p_render_data && p_render_data->shadow_atlas.is_valid()) {
			texture = light_storage->shadow_atlas_get_texture(p_render_data->shadow_atlas);
		}
		if (!texture.is_valid()) {
			texture = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH);
		}
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, texture));
	}

	{
		RID texture;
		if (p_use_directional_shadow_atlas && light_storage->directional_shadow_get_texture().is_valid()) {
			texture = light_storage->directional_shadow_get_texture();
		} else {
			texture = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH);
		}
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, texture));
	}

	{
		RID cb = (p_render_data && p_render_data->cluster_buffer.is_valid()) ? p_render_data->cluster_buffer : scene_shader.default_vec4_xform_buffer;
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, cb));
	}

	p_samplers.append_uniforms(uniforms, 7);

	// Dummy: screen depth/color textures (hint_depth_texture / hint_screen_texture, used by BaseMaterial3D proximity fade / refraction) are not supported.
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 19, texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH)));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 20, texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK)));

	{
		RID aot = rb.is_valid() && rb->has_texture(RB_SCOPE_SSAO, RB_FINAL) ? rb->get_texture(RB_SCOPE_SSAO, RB_FINAL) : RID();
		RID texture = aot.is_valid() ? aot : texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 21, texture));
	}

	return UniformSetCacheRD::get_singleton()->get_cache_vec(scene_shader.default_shader_rd, RENDER_PASS_UNIFORM_SET, uniforms);
}

RID RenderVsDeferredClustered::_render_buffers_get_normal_texture(Ref<RenderSceneBuffersRD> p_render_buffers) {
	Ref<RenderBufferDataVsDeferredClustered> rb_data = p_render_buffers->get_custom_data(RB_SCOPE_VS_DEFERRED_CLUSTERED);
	return rb_data.is_valid() && rb_data->has_normal_roughness() ? rb_data->get_normal_roughness() : RID();
}

void RenderVsDeferredClustered::environment_set_ssao_quality(RSE::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_quality < RSE::EnvironmentSSAOQuality::ENV_SSAO_QUALITY_VERY_LOW || p_quality > RSE::EnvironmentSSAOQuality::ENV_SSAO_QUALITY_ULTRA);
	ss_effects->ssao_set_quality(p_quality, p_half_size, p_adaptive_target, p_blur_passes, p_fadeout_from, p_fadeout_to);
}

/* Geometry instances */

RenderVsDeferredClustered *RenderVsDeferredClustered::singleton = nullptr;

void RenderVsDeferredClustered::GeometryInstanceVsDeferredClustered::_mark_dirty() {
	if (dirty_list_element.in_list()) {
		return;
	}

	//clear surface caches
	GeometryInstanceSurfaceDataCache *surf = surface_caches;

	while (surf) {
		GeometryInstanceSurfaceDataCache *next = surf->next;
		RenderVsDeferredClustered::get_singleton()->geometry_instance_surface_alloc.free(surf);
		surf = next;
	}

	surface_caches = nullptr;

	RenderVsDeferredClustered::get_singleton()->geometry_instance_dirty_list.add(&dirty_list_element);
}

void RenderVsDeferredClustered::_geometry_instance_add_surface_with_material(GeometryInstanceVsDeferredClustered *ginstance, uint32_t p_surface, SceneShaderVsDeferredClustered::MaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	uint32_t flags = 0;

	if (ginstance->data->cast_double_sided_shadows) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_DOUBLE_SIDED_SHADOWS;
	}

	if (p_material->shader_data->uses_alpha_pass()) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA;
		if (p_material->shader_data->uses_depth_in_alpha_pass()) {
			flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH;
			flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW;
		}
	} else {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE;
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH;
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW;
	}

	SceneShaderVsDeferredClustered::MaterialData *material_shadow = nullptr;
	void *surface_shadow = nullptr;
	if (p_material->shader_data->uses_shared_shadow_material()) {
		material_shadow = static_cast<SceneShaderVsDeferredClustered::MaterialData *>(RendererRD::MaterialStorage::get_singleton()->material_get_data(scene_shader.default_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));

		RID shadow_mesh = mesh_storage->mesh_get_shadow_mesh(p_mesh);
		if (shadow_mesh.is_valid()) {
			surface_shadow = mesh_storage->mesh_get_surface(shadow_mesh, p_surface);
		}
	} else {
		material_shadow = p_material;
	}

	GeometryInstanceSurfaceDataCache *sdcache = geometry_instance_surface_alloc.alloc();

	sdcache->flags = flags;

	sdcache->shader = p_material->shader_data;
	sdcache->material = p_material;
	sdcache->material_uniform_set = p_material->uniform_set;
	sdcache->surface = mesh_storage->mesh_get_surface(p_mesh, p_surface);
	sdcache->primitive = mesh_storage->mesh_surface_get_primitive(sdcache->surface);
	sdcache->surface_index = p_surface;

	if (ginstance->data->dirty_dependencies) {
		RSG::utilities->base_update_dependency(p_mesh, &ginstance->data->dependency_tracker);
	}

	//shadow
	sdcache->shader_shadow = material_shadow->shader_data;
	sdcache->material_uniform_set_shadow = material_shadow->uniform_set;

	sdcache->surface_shadow = surface_shadow ? surface_shadow : sdcache->surface;

	sdcache->owner = ginstance;

	sdcache->next = ginstance->surface_caches;
	ginstance->surface_caches = sdcache;

	//sortkey

	sdcache->sort.sort_key1 = 0;
	sdcache->sort.sort_key2 = 0;

	sdcache->sort.surface_index = p_surface;
	sdcache->sort.material_id_hi = (p_material_id & 0xFF000000) >> 24;
	sdcache->sort.material_id_lo = (p_material_id & 0x00FFFFFF);
	sdcache->sort.shader_id = p_shader_id;
	sdcache->sort.geometry_id = p_mesh.get_local_index(); //only meshes can repeat anyway
	sdcache->sort.priority = p_material->priority;
}

void RenderVsDeferredClustered::_geometry_instance_add_surface(GeometryInstanceVsDeferredClustered *ginstance, uint32_t p_surface, RID p_material, RID p_mesh) {
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();
	RID m_src = ginstance->data->material_override.is_valid() ? ginstance->data->material_override : p_material;

	SceneShaderVsDeferredClustered::MaterialData *material = nullptr;

	if (m_src.is_valid()) {
		material = static_cast<SceneShaderVsDeferredClustered::MaterialData *>(material_storage->material_get_data(m_src, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		if (!material || !material->shader_data->is_valid()) {
			material = nullptr;
		}
	}

	if (material) {
		if (ginstance->data->dirty_dependencies) {
			material_storage->material_update_dependency(m_src, &ginstance->data->dependency_tracker);
		}
	} else {
		material = static_cast<SceneShaderVsDeferredClustered::MaterialData *>(material_storage->material_get_data(scene_shader.default_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		m_src = scene_shader.default_material;
	}

	ERR_FAIL_NULL(material);

	// Note: material next_pass and material_overlay are not supported.
	_geometry_instance_add_surface_with_material(ginstance, p_surface, material, m_src.get_local_index(), material_storage->material_get_shader_id(m_src), p_mesh);
}

void RenderVsDeferredClustered::_geometry_instance_update(RenderGeometryInstance *p_geometry_instance) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	GeometryInstanceVsDeferredClustered *ginstance = static_cast<GeometryInstanceVsDeferredClustered *>(p_geometry_instance);

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_begin();
	}

	//add geometry for drawing
	switch (ginstance->data->base_type) {
		case RSE::INSTANCE_MESH: {
			uint32_t surface_count;
			RID mesh = ginstance->data->base;

			const RID *materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
			if (materials) {
				//if no materials, no surfaces.
				const RID *inst_materials = ginstance->data->surface_materials.ptr();
				uint32_t surf_mat_count = ginstance->data->surface_materials.size();

				for (uint32_t j = 0; j < surface_count; j++) {
					RID material = (j < surf_mat_count && inst_materials[j].is_valid()) ? inst_materials[j] : materials[j];
					_geometry_instance_add_surface(ginstance, j, material, mesh);
				}
			}

			ginstance->instance_count = 1;

		} break;

		case RSE::INSTANCE_MULTIMESH: {
			RID mesh = mesh_storage->multimesh_get_mesh(ginstance->data->base);
			if (mesh.is_valid()) {
				uint32_t surface_count;

				const RID *materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
				if (materials) {
					for (uint32_t j = 0; j < surface_count; j++) {
						_geometry_instance_add_surface(ginstance, j, materials[j], mesh);
					}
				}

				ginstance->instance_count = mesh_storage->multimesh_get_instances_to_draw(ginstance->data->base);
			}

		} break;

		default: { // Particles are not supported (no surfaces, nothing is drawn).
		}
	}

	//Fill push constant

	ginstance->base_flags = 0;

	if (ginstance->data->base_type == RSE::INSTANCE_MULTIMESH) {
		ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH;

		if (mesh_storage->multimesh_get_transform_format(ginstance->data->base) == RSE::MULTIMESH_TRANSFORM_2D) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D;
		}
		if (mesh_storage->multimesh_uses_colors(ginstance->data->base)) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR;
		}
		if (mesh_storage->multimesh_uses_custom_data(ginstance->data->base)) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA;
		}
		if (mesh_storage->multimesh_uses_indirect(ginstance->data->base)) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT;
		}

		ginstance->transforms_uniform_set = mesh_storage->multimesh_get_3d_uniform_set(ginstance->data->base, scene_shader.default_shader_rd, TRANSFORMS_UNIFORM_SET);

	} else if (ginstance->data->base_type == RSE::INSTANCE_MESH) {
		if (mesh_storage->skeleton_is_valid(ginstance->data->skeleton)) {
			ginstance->transforms_uniform_set = mesh_storage->skeleton_get_3d_uniform_set(ginstance->data->skeleton, scene_shader.default_shader_rd, TRANSFORMS_UNIFORM_SET);
			if (ginstance->data->dirty_dependencies) {
				mesh_storage->skeleton_update_dependency(ginstance->data->skeleton, &ginstance->data->dependency_tracker);
			}
		} else {
			ginstance->transforms_uniform_set = RID();
		}
	}

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_end();
		ginstance->data->dirty_dependencies = false;
	}

	ginstance->dirty_list_element.remove_from_list();
}

void RenderVsDeferredClustered::_update_dirty_geometry_instances() {
	while (geometry_instance_dirty_list.first()) {
		_geometry_instance_update(geometry_instance_dirty_list.first()->self());
	}
}

void RenderVsDeferredClustered::_geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker) {
	switch (p_notification) {
		case Dependency::DEPENDENCY_CHANGED_MATERIAL:
		case Dependency::DEPENDENCY_CHANGED_MESH:
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH:
		case Dependency::DEPENDENCY_CHANGED_SKELETON_DATA: {
			static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
			static_cast<GeometryInstanceVsDeferredClustered *>(p_tracker->userdata)->data->dirty_dependencies = true;
		} break;
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH_VISIBLE_INSTANCES: {
			GeometryInstanceVsDeferredClustered *ginstance = static_cast<GeometryInstanceVsDeferredClustered *>(p_tracker->userdata);
			if (ginstance->data->base_type == RSE::INSTANCE_MULTIMESH) {
				ginstance->instance_count = RendererRD::MeshStorage::get_singleton()->multimesh_get_instances_to_draw(ginstance->data->base);
			}
		} break;
		default: {
			//rest of notifications of no interest
		} break;
	}
}

void RenderVsDeferredClustered::_geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker) {
	static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
	static_cast<GeometryInstanceVsDeferredClustered *>(p_tracker->userdata)->data->dirty_dependencies = true;
}

RenderGeometryInstance *RenderVsDeferredClustered::geometry_instance_create(RID p_base) {
	RSE::InstanceType type = RSG::utilities->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RSE::INSTANCE_GEOMETRY_MASK), nullptr);

	GeometryInstanceVsDeferredClustered *ginstance = geometry_instance_alloc.alloc();
	ginstance->data = memnew(GeometryInstanceVsDeferredClustered::Data);

	ginstance->data->base = p_base;
	ginstance->data->base_type = type;
	ginstance->data->dependency_tracker.userdata = ginstance;
	ginstance->data->dependency_tracker.changed_callback = _geometry_instance_dependency_changed;
	ginstance->data->dependency_tracker.deleted_callback = _geometry_instance_dependency_deleted;

	ginstance->_mark_dirty();

	return ginstance;
}

void RenderVsDeferredClustered::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceVsDeferredClustered *ginstance = static_cast<GeometryInstanceVsDeferredClustered *>(p_geometry_instance);
	ERR_FAIL_NULL(ginstance);
	GeometryInstanceSurfaceDataCache *surf = ginstance->surface_caches;
	while (surf) {
		GeometryInstanceSurfaceDataCache *next = surf->next;
		geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	memdelete(ginstance->data);
	geometry_instance_alloc.free(ginstance);
}

/* Misc */

uint32_t RenderVsDeferredClustered::get_pipeline_compilations(RSE::PipelineSource p_source) {
	return scene_shader.get_pipeline_compilations(p_source);
}

String RenderVsDeferredClustered::get_name() const {
	return "vs_deferred_clustered";
}

// Runs a one-off compute shader that fills a lookup texture (used for best fit normals and the DFG LUT).
template <typename T>
static void _create_lut(T &p_lut, RD::DataFormat p_format, uint32_t p_size) {
	Vector<String> modes;
	modes.push_back("\n");
	p_lut.shader.initialize(modes);
	p_lut.shader_version = p_lut.shader.version_create();
	RID shader = p_lut.shader.version_get_shader(p_lut.shader_version, 0);
	ERR_FAIL_COND(shader.is_null());
	p_lut.pipeline = RD::get_singleton()->compute_pipeline_create(shader);

	RD::TextureFormat tformat;
	tformat.format = p_format;
	tformat.width = p_size;
	tformat.height = p_size;
	tformat.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
	tformat.texture_type = RD::TEXTURE_TYPE_2D;
	p_lut.texture = RD::get_singleton()->texture_create(tformat, RD::TextureView());

	Vector<RD::Uniform> uniforms;
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, p_lut.texture));
	RID uniform_set = RD::get_singleton()->uniform_set_create(uniforms, shader, 0);

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, p_lut.pipeline);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, tformat.width, tformat.height, 1);
	RD::get_singleton()->compute_list_end();
}

template <typename T>
static void _free_lut(T &p_lut) {
	RD::get_singleton()->free_rid(p_lut.pipeline);
	RD::get_singleton()->free_rid(p_lut.texture);
	p_lut.shader.version_free(p_lut.shader_version);
}

RenderVsDeferredClustered::RenderVsDeferredClustered() {
	singleton = this;

	/* SCENE SHADER */
	{
		String defines;
		defines += "\n#define MAX_ROUGHNESS_LOD " + itos(get_roughness_layers() - 1) + ".0\n";
		if (is_using_radiance_octmap_array()) {
			defines += "\n#define USE_RADIANCE_OCTMAP_ARRAY \n";
		}
		defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS " + itos(MAX_DIRECTIONAL_LIGHTS) + "\n";

		bool specular_occlusion = GLOBAL_GET("rendering/reflections/specular_occlusion/enabled");
		if (!specular_occlusion) {
			defines += "\n#define SPECULAR_OCCLUSION_DISABLED\n";
		}

		defines += "\n#define MATERIAL_UNIFORM_SET " + itos(MATERIAL_UNIFORM_SET) + "\n";
#ifdef REAL_T_IS_DOUBLE
		defines += "\n#define USE_DOUBLE_PRECISION \n";
#endif

		scene_shader.init(defines);
	}

	_create_lut(best_fit_normal, RD::DATA_FORMAT_R8_UNORM, 1024);
	_create_lut(dfg_lut, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, 128);

	ss_effects = memnew(RendererRD::SSEffects);
}

RenderVsDeferredClustered::~RenderVsDeferredClustered() {
	if (ss_effects != nullptr) {
		memdelete(ss_effects);
		ss_effects = nullptr;
	}

	RSG::light_storage->directional_shadow_atlas_set_size(0);

	_free_lut(best_fit_normal);
	_free_lut(dfg_lut);

	for (const RID &rid : scene_state.uniform_buffers) {
		RD::get_singleton()->free_rid(rid);
	}
	for (const RID &rid : scene_state.implementation_uniform_buffers) {
		RD::get_singleton()->free_rid(rid);
	}
	for (uint32_t i = 0; i < RENDER_LIST_MAX; i++) {
		scene_state.instance_buffer[i].uninit();
	}
}
