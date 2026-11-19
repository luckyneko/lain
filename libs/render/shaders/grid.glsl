// A viewport's reference grid (render::Grid), for fragment shaders: it reads screen-space derivatives,
// which a vertex shader has none of. Needs frame.glsl first.

// How much of this pixel the nearest line along each axis covers, 0 to 1, for coordinates counted in
// lines: a line is 1 px wide, falling off over a pixel each side, so it is anti-aliased whatever its
// angle. Where an axis's lines come closer than a few pixels apart they alias into moire, so they fade
// out first, each axis by its own spacing on screen.
float gridLineCoverage(vec2 coordinate)
{
	const vec2 perPixel = max(fwidth(coordinate), vec2(1e-6));
	const vec2 pixels = abs(fract(coordinate - 0.5) - 0.5) / perPixel; // pixels to the nearest line
	const vec2 fade = 1.0 - smoothstep(0.15, 0.3, perPixel);
	const vec2 covered = (1.0 - min(pixels, 1.0)) * fade;
	return max(covered.x, covered.y);
}

// The grid at world point `p`: its colour in rgb, and in a how much of this pixel its lines cover. Every
// call takes derivatives, so it must be made in uniform control flow, and its answer used conditionally.
vec4 gridAt(vec3 p)
{
	const vec3 offset = p - frame.gridOrigin.xyz;
	const vec2 metres = vec2(dot(offset, frame.gridU.xyz), dot(offset, frame.gridV.xyz));
	const vec2 lines = metres / frame.gridOrigin.w;
	const float minor = gridLineCoverage(lines);
	const float major = frame.gridMinor.w > 0.5 ? gridLineCoverage(lines / frame.gridMinor.w) : 0.0;
	const bool inside = abs(metres.x) <= frame.gridU.w && abs(metres.y) <= frame.gridV.w;
	const vec3 colour = major >= minor ? frame.gridMajor.rgb : frame.gridMinor.rgb;
	return vec4(colour, inside ? max(minor, major) : 0.0);
}

// The grid's plane's unit normal.
vec3 gridNormal()
{
	return normalize(cross(frame.gridU.xyz, frame.gridV.xyz));
}
