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
	// Pulled toward the camera by 1e-4 of the depth buffer, so a line on a surface is drawn on it. A
	// 1 px line's depth is the same across its width while an oblique surface's is not, so with no
	// pull a multisampled line on a floor loses about half its samples. Measured on Metal: 0 of 26
	// points along floor lines drawn whole with no pull, 15 at 2e-6, all from 2e-5; a line 5 cm behind
	// a wall 2 m away stays hidden up to 1e-3 (test_viewport.cpp brackets it from both sides).
	gl_Position.z -= 1e-4 * gl_Position.w;
	colour = vertex.colour.rgb;
}
