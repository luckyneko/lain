#version 450

// A scene's surfaces (lain::render, ADR-0028): a mesh's vertices into the world, then through the
// view's pinhole into clip space. The blocks here must match renderer.cpp's FrameConstants and
// ObjectConstants.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(set = 0, binding = 0) uniform Frame
{
	mat4 clipFromWorld;
	vec4 eye; // the camera's centre, in the world
	vec4 ambient;
	uint lightCount;
}
frame;

layout(set = 0, binding = 1) uniform Object
{
	mat4 worldFromMesh;
	mat4 normalFromMesh; // the inverse transpose of worldFromMesh's 3x3, so a scaled mesh's normals stay normal
	uint flags;			 // bit 0: the mesh has normals
}
object;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;

void main()
{
	const vec4 world = object.worldFromMesh * vec4(inPosition, 1.0);
	worldPosition = world.xyz;
	worldNormal = mat3(object.normalFromMesh) * inNormal;
	uv = inUv;
	gl_Position = frame.clipFromWorld * world;
}
