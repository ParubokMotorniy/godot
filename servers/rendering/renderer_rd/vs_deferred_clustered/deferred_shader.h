#pragma once

#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shaders/vs_deferred_clustered/deferred_shading_passes.glsl.gen.h"

class DeferredShader {
public:
	DeferredShader();
	~DeferredShader();

	// Uses sort pass preprocessing results to shade pixels according to the visible surface
	struct ShadingBuffers {
	};

	void allocate_shading_buffers(ShadingBuffers &p_buffers, const Size2i &p_size);
	static void free_shading_buffers(ShadingBuffers &p_buffers);
	void shade_pixels();

private:
	enum ShadeMode {
		SHADE_MODE_ZERO,
		SHADE_MODE_MAX
	};

	struct SortPass {
		DeferredShadingPassesShaderRD shader;
		RID shader_version;
		PipelineDeferredRD pipelines[SHADE_MODE_MAX];
	} sort_pass;
};
