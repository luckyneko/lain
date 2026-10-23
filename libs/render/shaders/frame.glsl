// The per-view constants every drawing reads at binding 0, and the display encoding a viewport writes.
// Must match state.h's FrameConstants.

layout(set = 0, binding = 0) uniform Frame
{
	mat4 clipFromWorld;
	vec4 eye; // the camera's centre, in the world
	vec4 ambient;
	uint lightCount;
	uint encode; // 1: write lain's sRGB curve, for a display; 0: linear light
}
frame;

// Linear light through the sRGB curve (lain::image's), for an 8-bit UNORM target ImGui composes as
// sRGB-encoded, or linear light unchanged.
vec3 forTarget(vec3 linear)
{
	if (frame.encode == 0u)
		return linear;
	const vec3 c = clamp(linear, 0.0, 1.0);
	return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, 12.92 * c, lessThanEqual(c, vec3(0.0031308)));
}
