#version 450
#extension GL_GOOGLE_include_directive : require

// A viewport's overlay lines (lain::render::Segment): two vertices a segment, read from a storage
// buffer by index, since the lines change every frame and a vertex buffer is written through a
// staging copy that waits for the device. Must match state.h's LineVertex.

#include "frame.glsl"

struct LineVertex
{
	vec4 position; // in the world
	vec4 colour;   // linear light
};

layout(std430, set = 0, binding = 1) readonly buffer LineVertices
{
	LineVertex vertices[];
};

layout(location = 0) out vec3 colour;

void main()
{
	const LineVertex vertex = vertices[gl_VertexIndex];
	gl_Position = frame.clipFromWorld * vec4(vertex.position.xyz, 1.0);
	// Pulled toward the camera by 1e-4 of the depth buffer, so a line on a surface facing the camera is
	// drawn on it rather than lost in it (test_viewport.cpp brackets the value: with no pull the line on
	// the wall is hidden, and at 3e-3 the line 5 cm behind it shows). It does not help a line on a
	// surface seen obliquely: there a multisampled line loses samples to the surface whatever the pull
	// (measured to 2e-3), because its samples lie up to 0.375 px off its centre, where the surface is
	// nearer. Such a line keeps at least a quarter of its coverage, but is never drawn whole.
	gl_Position.z -= 1e-4 * gl_Position.w;
	colour = vertex.colour.rgb;
}
