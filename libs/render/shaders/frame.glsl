// The per-view constants every drawing reads at binding 0, and the display encoding a viewport writes.
// Must match state.h's FrameConstants.

layout(set = 0, binding = 0) uniform Frame
{
	mat4 clipFromWorld;
	vec4 eye; // the camera's centre, in the world
	vec4 ambient;
	vec4 cameraAxis; // the camera's +Z in clip space, for offPlane()
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

// `clip` moved off the camera's own plane when it lies almost in it. Such a vertex is never seen (the
// near plane clips it), but the CPU device (SwiftShader) then draws its triangle's depth wrong, and the
// surface hides everything behind it: measured with a floor reaching the camera's plane, it failed
// wherever |w| was under about 1e-5 of the vertex's |x| or |y|, wider surfaces failing further from
// the plane. Moved along the camera's axis to |w| = 1e-4 of that, ten times clear, it draws as every
// other device draws it. The move is a real one in the world, of at most 2e-4 of the vertex's clip |x|
// or |y| in metres: a few millimetres for a vertex 10 m from the axis, which the camera cannot see.
vec4 offPlane(vec4 clip)
{
	const float least = 1e-4 * max(abs(clip.x), abs(clip.y));
	if (abs(clip.w) >= least)
		return clip;
	const float target = clip.w < 0.0 ? -least : least;
	return clip + frame.cameraAxis * (target - clip.w);
}
