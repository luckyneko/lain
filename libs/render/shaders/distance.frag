#version 450
#extension GL_GOOGLE_include_directive : require

// How far the view's centre is from each surface it sees (lain::render::Renderer::renderDistance,
// ADR-0028), for the visibility map to compare a point's own distance with. Along the ray, not z, so a
// comparison means the same on any face of a camera's cube. From the world position scene.vert hands
// on, interpolated perspective-correctly, so a texel's value is where its centre's ray meets the
// surface. The blocks here must match state.h's.

#include "frame.glsl"

layout(location = 0) in vec3 worldPosition;

layout(location = 0) out float outDistance;

void main()
{
	outDistance = distance(worldPosition, frame.eye.xyz);
}
