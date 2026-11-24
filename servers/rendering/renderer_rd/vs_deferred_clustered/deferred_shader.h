#pragma once

#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shaders/vs_deferred_clustered/deferred_shading_pass.glsl.gen.h"

namespace RendererSceneRenderImplementation {


class DeferredShader {
public:
	DeferredShader(const String &p_defines, RID p_default_vec4_xform_buffer);
	~DeferredShader();

	RID get_shader_rd() const { return shader_rd; }
	RID get_pipeline() { return pipeline.get_rid(); }

	RID default_vec4_xform_uniform_set;

private:
	DeferredShadingPassShaderRD shader;
	RID shader_version;
	RID shader_rd;
	PipelineDeferredRD pipeline;
};

} //namespace RendererSceneRenderImplementation
