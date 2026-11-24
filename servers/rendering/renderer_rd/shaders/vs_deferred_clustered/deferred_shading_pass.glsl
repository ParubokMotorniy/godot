#[compute]

#version 450

#VERSION_DEFINES

#include "../half_inc.glsl"
#include "scene_vs_deferred_clustered_inc.glsl"
// TODO: scene_vs_deferred_clustered_lights_inc.glsl is fragment-only for now (the shadow filters read
// gl_FragCoord)

// TODO: figure this out
// layout(set = X, binding = Y) uniform usampler2D visibility_buffer;

void main() {
	//TODO: shade pixels from the bound pixel list according to primitive+instance in visbuffer
}
