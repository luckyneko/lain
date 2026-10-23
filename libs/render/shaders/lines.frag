#version 450
#extension GL_GOOGLE_include_directive : require

// A viewport's overlay lines, in their own colours, unlit.

#include "frame.glsl"

layout(location = 0) in vec3 colour;

layout(location = 0) out vec4 outColour;

void main()
{
	outColour = vec4(forTarget(colour), 1.0);
}
