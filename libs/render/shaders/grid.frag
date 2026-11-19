#version 450
#extension GL_GOOGLE_include_directive : require

// A viewport's grid backdrop: the grid's lines blended over what is already drawn, with alpha their
// coverage, so the backdrop is transparent between them. Blended in the target's encoding, as a surface
// on the plane mixes them (scene.frag), so the two draw one line alike.

#include "frame.glsl"
#include "grid.glsl"

layout(location = 0) in vec3 worldPosition;

layout(location = 0) out vec4 outColour;

void main()
{
	const vec4 grid = gridAt(worldPosition);
	outColour = vec4(forTarget(grid.rgb), grid.a);
}
