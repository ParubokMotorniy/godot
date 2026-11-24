#include "deferred_shader.h"

#include "servers/rendering/renderer_rd/vs_deferred_clustered/render_vs_deferred_clustered.h"
#include "servers/rendering/rendering_device.h"

using namespace RendererSceneRenderImplementation;

DeferredShader::DeferredShader(const String &p_defines, RID p_default_vec4_xform_buffer) {
	Vector<String> modes;
	modes.push_back("");

	Vector<uint64_t> dynamic_buffers;
	dynamic_buffers.push_back(ShaderRD::DynamicBuffer::encode(RenderVsDeferredClustered::RENDER_PASS_UNIFORM_SET, 2));
	shader.initialize(modes, p_defines, Vector<RD::PipelineImmutableSampler>(), dynamic_buffers);

	shader_version = shader.version_create();
	shader_rd = shader.version_get_shader(shader_version, 0);
	ERR_FAIL_COND(shader_rd.is_null());

	pipeline.create_compute_pipeline(shader_rd);

	Vector<RD::Uniform> uniforms;
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, p_default_vec4_xform_buffer));
	default_vec4_xform_uniform_set = RD::get_singleton()->uniform_set_create(uniforms, shader_rd, RenderVsDeferredClustered::TRANSFORMS_UNIFORM_SET);
}

DeferredShader::~DeferredShader() {
	if (default_vec4_xform_uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(default_vec4_xform_uniform_set)) {
		RD::get_singleton()->free_rid(default_vec4_xform_uniform_set);
	}
	pipeline.free();
	shader.version_free(shader_version);
}
