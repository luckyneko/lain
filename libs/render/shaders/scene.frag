#version 450
#extension GL_GOOGLE_include_directive : require

// A scene's surfaces, shaded (lain::render::Material, ADR-0028): Lambert's diffuse times the
// material's texture, plus Blinn-Phong's specular, under point and spot lights whose light falls
// with the square of the distance, plus the scene's ambient light. Lit from whichever side the
// camera sees. The blocks here must match state.h's.

#include "frame.glsl"

layout(set = 0, binding = 1) uniform Object
{
	mat4 worldFromMesh;
	mat4 normalFromMesh;
	uint flags;
}
object;

layout(set = 0, binding = 2) uniform MaterialBlock
{
	vec4 diffuse;  // w: 1 when the material has a texture
	vec4 specular; // w: Blinn-Phong's exponent
}
material;

// A material without a texture of its own has a white one bound here, which it never uses: a device's
// filtering need not return exactly 1 for it (the CPU device's reads 0.99989).
layout(set = 0, binding = 3) uniform sampler2D diffuseMap;

struct Light
{
	vec4 position; // w: 0 a point, 1 a spot
	vec4 direction; // a spot's axis, unit length
	vec4 radiance;	// colour times intensity
	vec4 cone;		// x: the cosine of the inner angle, y: of the outer
};

layout(std430, set = 0, binding = 4) readonly buffer Lights
{
	Light lights[];
};

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outColour;

void main()
{
	// A mesh without normals is shaded flat, by the face's own normal.
	vec3 n = (object.flags & 1u) != 0u ? normalize(worldNormal) : normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
	const vec3 v = normalize(frame.eye.xyz - worldPosition);
	if (dot(n, v) < 0.0)
		n = -n;

	// OBJ's v runs up and an image's rows run down. fract repeats the picture, and the gradients of the
	// unrepeated coordinates keep the mip level steady across the seam.
	const vec2 st = vec2(uv.x, 1.0 - uv.y);
	const vec3 sampled = textureGrad(diffuseMap, fract(st), dFdx(st), dFdy(st)).rgb;
	const vec3 albedo = material.diffuse.rgb * (material.diffuse.w > 0.5 ? sampled : vec3(1.0));

	vec3 colour = frame.ambient.rgb * albedo;
	for (uint i = 0u; i < frame.lightCount; ++i)
	{
		const Light light = lights[i];
		const vec3 toLight = light.position.xyz - worldPosition;
		const float distanceSquared = max(dot(toLight, toLight), 1e-12);
		const vec3 l = toLight * inversesqrt(distanceSquared);
		float spot = 1.0;
		if (light.position.w > 0.5)
			spot = smoothstep(light.cone.y, light.cone.x, dot(-l, light.direction.xyz));
		const vec3 irradiance = light.radiance.rgb * (spot / distanceSquared);
		const float lambert = dot(n, l);
		if (lambert <= 0.0)
			continue;
		colour += albedo * irradiance * lambert;
		if (material.specular.w > 0.0)
		{
			const vec3 h = normalize(l + v);
			colour += material.specular.rgb * irradiance * pow(max(dot(n, h), 0.0), material.specular.w);
		}
	}
	outColour = vec4(forTarget(colour), 1.0);
}
