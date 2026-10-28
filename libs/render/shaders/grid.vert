#version 450
#extension GL_GOOGLE_include_directive : require

// A viewport's grid backdrop (render::Grid): a quad on the grid's plane over the grid's extent, from
// the frame's constants alone, so it needs no vertex buffer. Pushed back by a hair of the depth buffer,
// so a surface lying on the plane, which draws the grid's lines itself (scene.frag), always wins the
// depth test there, while a surface clearly in front of the plane hides it and one behind it does not.

#include "frame.glsl"

layout(location = 0) out vec3 worldPosition;

// How far the quad is pushed back, in the depth buffer's own units: enough that a surface drawn on the
// same plane by other triangles always wins, as the line pass's pull is enough that a line on a facing
// surface does (shaders/lines.vert); in metres it grows as Z squared over the near plane.
const float kPushBack = 1e-4;

void main()
{
	const vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
	const vec2 corner = corners[gl_VertexIndex];
	worldPosition = frame.gridOrigin.xyz + corner.x * frame.gridU.w * frame.gridU.xyz + corner.y * frame.gridV.w * frame.gridV.xyz;
	vec4 clip = offPlane(frame.clipFromWorld * vec4(worldPosition, 1.0));
	clip.z += kPushBack * clip.w;
	gl_Position = clip;
}
