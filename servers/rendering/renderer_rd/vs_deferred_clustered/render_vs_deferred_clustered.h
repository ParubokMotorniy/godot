/**************************************************************************/
/*  render_vs_deferred_clustered.h                                            */
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

#pragma once

#include "core/templates/paged_allocator.h"
#include "servers/rendering/multi_uma_buffer.h"
#include "servers/rendering/renderer_rd/cluster_builder_rd.h"
#include "servers/rendering/renderer_rd/effects/ss_effects.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/shaders/best_fit_normal.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/integrate_dfg.glsl.gen.h"
#include "servers/rendering/renderer_rd/vs_deferred_clustered/scene_shader_vs_deferred_clustered.h"

#define RB_SCOPE_VS_DEFERRED_CLUSTERED SNAME("vs_deferred_clustered")

#define RB_TEX_NORMAL_ROUGHNESS SNAME("normal_roughness")

#define RB_BUF_VISIBILITY SNAME("visibility_buffer")

namespace RendererSceneRenderImplementation {

/* Minimal clustered renderer used as the base for a visibility buffer renderer.
 *
 * Frame (see _render_scene()):
 *   1. depth pre-pass (optionally writing normal/roughness, needed by SSAO),
 *   2. shadow passes (directional atlas, positional atlas, cubemap + DP),
 *   3. SSAO compute pass (RendererRD::SSEffects),
 *   4. light/cluster setup,
 *   5. opaque color pass (clustered forward shading),
 *   6. sky,
 *   7. transparent color pass (back-to-front, blended),
 *   8. tonemap/post-process into the render target (shared code).
 */
class RenderVsDeferredClustered : public RendererSceneRenderRD {
	friend SceneShaderVsDeferredClustered;

	enum {
		SCENE_UNIFORM_SET = 0,
		RENDER_PASS_UNIFORM_SET = 1,
		TRANSFORMS_UNIFORM_SET = 2,
		MATERIAL_UNIFORM_SET = 3,
	};

	enum {
		INSTANCE_DATA_BUFFER_MIN_SIZE = 4096
	};

	enum RenderListType {
		RENDER_LIST_OPAQUE, //used for opaque objects
		RENDER_LIST_ALPHA, //used for transparent objects
		RENDER_LIST_SECONDARY, //used for shadows and other objects
		RENDER_LIST_MAX
	};

	/* Scene Shader */

	SceneShaderVsDeferredClustered scene_shader;

public:
	/* Framebuffer */

	class RenderBufferDataVsDeferredClustered : public RenderBufferCustomDataRD {
		GDCLASS(RenderBufferDataVsDeferredClustered, RenderBufferCustomDataRD)

	private:
		RenderSceneBuffersRD *render_buffers = nullptr;

	public:
		ClusterBuilderRD *cluster_builder = nullptr;
		RendererRD::SSEffects::SSAORenderBuffers ssao;

		void ensure_normal_roughness_texture();
		bool has_normal_roughness() const { return render_buffers->has_texture(RB_SCOPE_VS_DEFERRED_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS); }
		RID get_normal_roughness() const { return render_buffers->get_texture(RB_SCOPE_VS_DEFERRED_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS); }

		void ensure_visibility_buffer();
		bool has_visibility_buffer() const { return render_buffers->has_texture(RB_SCOPE_VS_DEFERRED_CLUSTERED, RB_BUF_VISIBILITY); }
		RID get_visibility_buffer() const { return render_buffers->get_texture(RB_SCOPE_VS_DEFERRED_CLUSTERED,RB_BUF_VISIBILITY); }

		RID get_color_fb();
		RID get_depth_fb(bool p_normal_roughness);
		RID get_visibility_fb();

		virtual void configure(RenderSceneBuffersRD *p_render_buffers) override;
		virtual void free_data() override;
	};

	// MSAA is not supported: render buffers are always configured without it, so the
	// single-sample color/depth textures are regular attachments.
	// Not a GDCLASS (never instantiated by name), so it shares the RenderSceneBuffersRD class info.
	class RenderSceneBuffersNoMSAA : public RenderSceneBuffersRD {
	public:
		virtual void configure(const RenderSceneBuffersConfiguration *p_config) override;
	};

	virtual Ref<RenderSceneBuffers> render_buffers_create() override;

private:
	virtual void setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) override;

	RID render_base_uniform_set;

	void _update_render_base_uniform_set();
	RID _setup_render_pass_uniform_set(RenderListType p_render_list, const RenderDataRD *p_render_data, RID p_radiance_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index, bool p_use_directional_shadow_atlas = false);

	struct BestFitNormal {
		BestFitNormalShaderRD shader;
		RID shader_version;
		RID pipeline;
		RID texture;
	} best_fit_normal;

	struct IntegrateDFG {
		IntegrateDfgShaderRD shader;
		RID shader_version;
		RID pipeline;
		RID texture;
	} dfg_lut;

	enum PassMode {
		PASS_MODE_COLOR,
		PASS_MODE_SHADOW,
		PASS_MODE_SHADOW_DP,
		PASS_MODE_DEPTH,
		PASS_MODE_DEPTH_NORMAL_ROUGHNESS,
		PASS_MODE_VISIBILITY,
	};

	enum ColorPassFlags {
		COLOR_PASS_FLAG_TRANSPARENT = 1 << 0,
	};

	struct GeometryInstanceSurfaceDataCache;
	struct RenderElementInfo;

	struct RenderListParameters {
		GeometryInstanceSurfaceDataCache **elements = nullptr;
		RenderElementInfo *element_info = nullptr;
		int element_count = 0;
		bool reverse_cull = false;
		PassMode pass_mode = PASS_MODE_COLOR;
		uint32_t color_pass_flags = 0;
		RID render_pass_uniform_set;
		uint32_t element_offset = 0;

		RenderListParameters(GeometryInstanceSurfaceDataCache **p_elements, RenderElementInfo *p_element_info, int p_element_count, bool p_reverse_cull, PassMode p_pass_mode, uint32_t p_color_pass_flags, RID p_render_pass_uniform_set, uint32_t p_element_offset = 0) {
			elements = p_elements;
			element_info = p_element_info;
			element_count = p_element_count;
			reverse_cull = p_reverse_cull;
			pass_mode = p_pass_mode;
			color_pass_flags = p_color_pass_flags;
			render_pass_uniform_set = p_render_pass_uniform_set;
			element_offset = p_element_offset;
		}
	};

	// When changing any of these enums, remember to change the corresponding enums in the shader files as well.
	enum {
		INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT = 1 << 2,
		INSTANCE_DATA_FLAGS_NON_UNIFORM_SCALE = 1 << 4,
		INSTANCE_DATA_FLAG_MULTIMESH = 1 << 12,
		INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D = 1 << 13,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR = 1 << 14,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA = 1 << 15,
		INSTANCE_DATA_FLAGS_FADE_SHIFT = 24,
		INSTANCE_DATA_FLAGS_FADE_MASK = 0xFFUL << INSTANCE_DATA_FLAGS_FADE_SHIFT
	};

	struct SceneState {
		// This struct is loaded into Set 1 - Binding 1, populated at start of rendering a frame, must match with shader code
		struct UBO {
			uint32_t cluster_shift;
			uint32_t cluster_width;
			uint32_t cluster_type_size;
			uint32_t max_cluster_element_count_div_32;

			uint32_t ss_effects_flags;
			float ssao_light_affect;
			float ssao_ao_affect;
			uint32_t pad1;
		};

		struct PushConstant {
			uint32_t base_index;
			uint32_t multimesh_offset;
			uint32_t pad[2];
		};

		struct InstanceData {
			float transform[12];
			float compressed_aabb_position[4];
			float compressed_aabb_size[4];
			float uv_scale[4];
			uint32_t flags;
			uint32_t layer_mask;
			uint32_t pad[2];
#ifdef REAL_T_IS_DOUBLE
			float model_precision[4];
#endif

			// These setters allow us to copy the data over with operation when using floats.
			inline void set_compressed_aabb(const AABB &p_aabb) {
#ifdef REAL_T_IS_DOUBLE
				compressed_aabb_position[0] = p_aabb.position.x;
				compressed_aabb_position[1] = p_aabb.position.y;
				compressed_aabb_position[2] = p_aabb.position.z;

				compressed_aabb_size[0] = p_aabb.size.x;
				compressed_aabb_size[1] = p_aabb.size.y;
				compressed_aabb_size[2] = p_aabb.size.z;
#else
				Vector3 *compressed_aabb_position_vec3 = reinterpret_cast<Vector3 *>(compressed_aabb_position);
				Vector3 *compressed_aabb_size_vec3 = reinterpret_cast<Vector3 *>(compressed_aabb_size);
				*compressed_aabb_position_vec3 = p_aabb.position;
				*compressed_aabb_size_vec3 = p_aabb.size;
#endif
			}

			inline void set_uv_scale(const Vector4 &p_uv_scale) {
#ifdef REAL_T_IS_DOUBLE
				uv_scale[0] = p_uv_scale.x;
				uv_scale[1] = p_uv_scale.y;
				uv_scale[2] = p_uv_scale.z;
				uv_scale[3] = p_uv_scale.w;
#else
				Vector4 *uv_scale_vec4 = reinterpret_cast<Vector4 *>(uv_scale);
				*uv_scale_vec4 = p_uv_scale;
#endif
			}
		};

		static_assert(std::is_trivially_destructible_v<InstanceData>);
		static_assert(std::is_trivially_constructible_v<InstanceData>);

		UBO ubo;

		LocalVector<RID> uniform_buffers;
		LocalVector<RID> implementation_uniform_buffers;
		uint32_t used_uniform_buffer_count = 0;

		MultiUmaBuffer<1u> instance_buffer[RENDER_LIST_MAX] = { MultiUmaBuffer<1u>("RENDER_LIST_OPAQUE"), MultiUmaBuffer<1u>("RENDER_LIST_ALPHA"), MultiUmaBuffer<1u>("RENDER_LIST_SECONDARY") };
		InstanceData *curr_gpu_ptr[RENDER_LIST_MAX] = {};

		struct ShadowPass {
			uint32_t element_from;
			uint32_t element_count;
			PassMode pass_mode;

			RID rp_uniform_set;

			RID framebuffer;
			Rect2i rect;
			bool clear_depth;
			bool flip_cull;

			uint32_t uniform_buffer_index;
		};

		LocalVector<ShadowPass> shadow_passes;

		void grow_instance_buffer(RenderListType p_render_list, uint32_t p_req_element_count, bool p_append);
	} scene_state;

	static RenderVsDeferredClustered *singleton;

	uint32_t _setup_environment(const RenderDataRD *p_render_data, const Size2i &p_screen_size, const Size2 &p_viewport_size, const Color &p_default_bg_color, bool p_opaque_render_buffers = false, bool p_pancake_shadows = false);

	struct RenderElementInfo {
		enum { MAX_REPEATS = (1 << 20) - 1 };
		union {
			struct {
				uint32_t lod_index : 8;
			};
			uint32_t value;
		};
		uint32_t repeat;
	};

	static_assert(std::is_trivially_destructible_v<RenderElementInfo>);
	static_assert(std::is_trivially_constructible_v<RenderElementInfo>);

	void _render_list(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element);
	void _render_list_with_draw_list(RenderListParameters *p_params, RID p_framebuffer, BitField<RD::DrawFlags> p_draw_flags = RD::DRAW_DEFAULT_ALL, const Vector<Color> &p_clear_color_values = Vector<Color>(), float p_clear_depth_value = 0.0, const Rect2 &p_region = Rect2());

	void _fill_instance_data(RenderListType p_render_list, int *p_render_info = nullptr, uint32_t p_offset = 0, int32_t p_max_elements = -1, bool p_update_buffer = true);
	void _fill_render_list(RenderListType p_render_list, const RenderDataRD *p_render_data, PassMode p_pass_mode, bool p_append = false);

	class GeometryInstanceVsDeferredClustered;

	// Cached data for drawing surfaces
	struct GeometryInstanceSurfaceDataCache {
		enum {
			FLAG_PASS_DEPTH = 1,
			FLAG_PASS_OPAQUE = 2,
			FLAG_PASS_ALPHA = 4,
			FLAG_PASS_SHADOW = 8,
			FLAG_USES_DOUBLE_SIDED_SHADOWS = 32768,
		};

		union {
			struct {
				uint64_t sort_key1;
				uint64_t sort_key2;
			};
			struct {
				// Needs to be grouped together to be used in RenderElementInfo, as the value is masked directly.
				uint64_t lod_index : 8;
				uint64_t reserved : 4;

				// Sorted based on optimal order for respecting priority and reducing the amount of rebinding of shaders, materials,
				// and geometry.
				uint64_t depth_layer : 4;
				uint64_t surface_index : 8;
				uint64_t geometry_id : 32;
				uint64_t material_id_hi : 8;

				uint64_t material_id_lo : 24;
				uint64_t shader_id : 32;
				uint64_t priority : 8;
			};
		} sort;

		RSE::PrimitiveType primitive = RSE::PRIMITIVE_MAX;
		uint32_t flags = 0;
		uint32_t surface_index = 0;
		uint32_t color_pass_inclusion_mask = 0;

		void *surface = nullptr;
		RID material_uniform_set;
		SceneShaderVsDeferredClustered::ShaderData *shader = nullptr;
		SceneShaderVsDeferredClustered::MaterialData *material = nullptr;

		void *surface_shadow = nullptr;
		RID material_uniform_set_shadow;
		SceneShaderVsDeferredClustered::ShaderData *shader_shadow = nullptr;

		GeometryInstanceSurfaceDataCache *next = nullptr;
		GeometryInstanceVsDeferredClustered *owner = nullptr;
	};

	class GeometryInstanceVsDeferredClustered : public RenderGeometryInstanceBase {
	public:
		//used during rendering
		RID transforms_uniform_set;
		uint32_t instance_count = 0;

		//used during setup
		GeometryInstanceSurfaceDataCache *surface_caches = nullptr;
		SelfList<GeometryInstanceVsDeferredClustered> dirty_list_element;

		GeometryInstanceVsDeferredClustered() :
				dirty_list_element(this) {}

		virtual void _mark_dirty() override;

		// Dummy: required by RenderGeometryInstance, lightmaps are not supported.
		virtual void set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) override {}
		// Dummy: required by RenderGeometryInstance, lightmaps are not supported.
		virtual void set_lightmap_capture(const Color *p_sh9) override {}

		virtual void clear_light_instances() override {}
		virtual void pair_light_instance(const RID p_light_instance, RSE::LightType light_type, uint32_t placement_idx) override {}
		virtual void pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) override {}
		virtual void pair_decal_instances(const RID *p_decal_instances, uint32_t p_decal_instance_count) override {}
		// Dummy: required by RenderGeometryInstance, VoxelGI is not supported.
		virtual void pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) override {}

		// Dummy: required by RenderGeometryInstance, light projectors and soft shadows are not supported.
		virtual void set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) override {}
	};

	// These are not used in the clustered path, it has different light clustering tech.
	virtual uint32_t get_max_lights_total() override { return 0; }
	virtual uint32_t get_max_lights_per_mesh() override { return 0; }

	static void _geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker);
	static void _geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker);

	SelfList<GeometryInstanceVsDeferredClustered>::List geometry_instance_dirty_list;

	PagedAllocator<GeometryInstanceVsDeferredClustered> geometry_instance_alloc;
	PagedAllocator<GeometryInstanceSurfaceDataCache> geometry_instance_surface_alloc;

	void _geometry_instance_add_surface_with_material(GeometryInstanceVsDeferredClustered *ginstance, uint32_t p_surface, SceneShaderVsDeferredClustered::MaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh);
	void _geometry_instance_add_surface(GeometryInstanceVsDeferredClustered *ginstance, uint32_t p_surface, RID p_material, RID p_mesh);
	void _geometry_instance_update(RenderGeometryInstance *p_geometry_instance);
	void _update_dirty_geometry_instances();

	/* Render List */

	struct RenderList {
		LocalVector<GeometryInstanceSurfaceDataCache *> elements;
		LocalVector<RenderElementInfo> element_info;

		void clear() {
			elements.clear();
			element_info.clear();
		}

		//should eventually be replaced by radix

		struct SortByKey {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->sort.sort_key2 == B->sort.sort_key2) ? (A->sort.sort_key1 < B->sort.sort_key1) : (A->sort.sort_key2 < B->sort.sort_key2);
			}
		};

		void sort_by_key() {
			SortArray<GeometryInstanceSurfaceDataCache *, SortByKey> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		void sort_by_key_range(uint32_t p_from, uint32_t p_size) {
			SortArray<GeometryInstanceSurfaceDataCache *, SortByKey> sorter;
			sorter.sort(elements.ptr() + p_from, p_size);
		}

		struct SortByReverseDepthAndPriority {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->sort.priority == B->sort.priority) ? (A->owner->depth > B->owner->depth) : (A->sort.priority < B->sort.priority);
			}
		};

		void sort_by_reverse_depth_and_priority() { //used for alpha

			SortArray<GeometryInstanceSurfaceDataCache *, SortByReverseDepthAndPriority> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		_FORCE_INLINE_ void add_element(GeometryInstanceSurfaceDataCache *p_element) {
			elements.push_back(p_element);
		}
	};

	RenderList render_list[RENDER_LIST_MAX];

	/* Effects */

	RendererRD::SSEffects *ss_effects = nullptr;

	/* Cluster builder */

	ClusterBuilderSharedDataRD cluster_builder_shared;
	ClusterBuilderRD *current_cluster_builder = nullptr;

	/* Render shadows */

	void _render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, bool p_open_pass, bool p_clear_region, RenderingServerTypes::RenderInfo *p_render_info, const Transform3D &p_main_cam_transform);
	void _render_shadow_begin();
	void _render_shadow_append(RID p_framebuffer, const PagedArray<RenderGeometryInstance *> &p_instances, const Projection &p_projection, const Transform3D &p_transform, float p_zfar, bool p_reverse_cull_face, bool p_use_dp, bool p_use_dp_flip, bool p_use_pancake, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, const Rect2i &p_rect, bool p_flip_y, bool p_clear_region, bool p_begin, RenderingServerTypes::RenderInfo *p_render_info, const Transform3D &p_main_cam_transform);
	void _render_shadow_process();
	void _render_shadow_end();

	/* Render Scene */
	void _render_shadows(RenderDataRD *p_render_data);
	void _process_ssao(RenderDataRD *p_render_data, RID p_normal_roughness);
	void _setup_lights(RenderDataRD *p_render_data);

protected:
	/* setup */

	virtual RID _render_buffers_get_normal_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override;
	virtual RID _render_buffers_get_velocity_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override { return RID(); } // Dummy: required by RendererSceneRenderRD, no motion vectors.

	virtual void environment_set_ssao_quality(RSE::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;
	// Dummy: required by RendererSceneRender, SSIL is not supported.
	virtual void environment_set_ssil_quality(RSE::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override {}
	// Dummy: required by RendererSceneRender, SSR is not supported.
	virtual void environment_set_ssr_half_size(bool p_half_size) override {}
	// Dummy: required by RendererSceneRender, SSR is not supported.
	virtual void environment_set_ssr_roughness_quality(RSE::EnvironmentSSRRoughnessQuality p_quality) override {}
	// Dummy: required by RendererSceneRender, SSS is not supported.
	virtual void sub_surface_scattering_set_quality(RSE::SubSurfaceScatteringQuality p_quality) override {}
	// Dummy: required by RendererSceneRender, SSS is not supported.
	virtual void sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) override {}

	/* Rendering */

	virtual void _render_scene(RenderDataRD *p_render_data, const Color &p_default_bg_color) override;

	// Dummy: required by RendererSceneRenderRD (material/UV2 baking for lightmapper and VoxelGI), not supported.
	virtual void _render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region, float p_exposure_normalization) override {}
	// Dummy: required by RendererSceneRenderRD (lightmap baking), not supported.
	virtual void _render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) override {}
	// Dummy: required by RendererSceneRenderRD, SDFGI is not supported.
	virtual void _render_sdfgi(Ref<RenderSceneBuffersRD> p_render_buffers, const Vector3i &p_from, const Vector3i &p_size, const AABB &p_bounds, const PagedArray<RenderGeometryInstance *> &p_instances, const RID &p_albedo_texture, const RID &p_emission_texture, const RID &p_emission_aniso_texture, const RID &p_geom_facing_texture, float p_exposure_normalization) override {}
	// Dummy: required by RendererSceneRenderRD, GPU particle heightfield colliders are not supported.
	virtual void _render_particle_collider_heightfield(RID p_fb, const Transform3D &p_cam_transform, const Projection &p_cam_projection, const PagedArray<RenderGeometryInstance *> &p_instances) override {}

public:
	static RenderVsDeferredClustered *get_singleton() { return singleton; }

	ClusterBuilderSharedDataRD *get_cluster_builder_shared() { return &cluster_builder_shared; }

	/* callback from updating our lighting UBOs, used to populate cluster builder */
	virtual void setup_added_light(const RSE::LightType p_type, const Transform3D &p_transform, float p_radius, float p_spot_aperture, const Vector2 &p_area_size) override;

	virtual void base_uniforms_changed() override;

	/* SDFGI UPDATE */

	// Dummy: required by RendererSceneRender, SDFGI is not supported.
	virtual void sdfgi_update(const Ref<RenderSceneBuffers> &p_render_buffers, RID p_environment, const Vector3 &p_world_position) override {}
	virtual int sdfgi_get_pending_region_count(const Ref<RenderSceneBuffers> &p_render_buffers) const override { return 0; } // Dummy: see above.
	virtual AABB sdfgi_get_pending_region_bounds(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override { return AABB(); } // Dummy: see above.
	virtual uint32_t sdfgi_get_pending_region_cascade(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override { return 0; } // Dummy: see above.

	/* GEOMETRY INSTANCE */

	virtual RenderGeometryInstance *geometry_instance_create(RID p_base) override;
	virtual void geometry_instance_free(RenderGeometryInstance *p_geometry_instance) override;

	virtual uint32_t geometry_instance_get_pair_mask() override { return 0; }

	/* PIPELINES */

	// Dummy: required by RendererSceneRender, pipelines are compiled synchronously on first use.
	virtual void mesh_generate_pipelines(RID p_mesh, bool p_background_compilation) override {}
	virtual uint32_t get_pipeline_compilations(RSE::PipelineSource p_source) override;

	/* SHADER LIBRARY */

	// Dummy: required by RenderingShaderLibrary, there are no optional shader groups.
	virtual void enable_features(BitField<FeatureBits> p_feature_bits) override {}
	virtual String get_name() const override;

	RenderVsDeferredClustered();
	~RenderVsDeferredClustered();
};
} // namespace RendererSceneRenderImplementation
