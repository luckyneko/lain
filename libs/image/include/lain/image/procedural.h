#pragma once

#include "lain/image/image.h"

#include <cstdint>

namespace lain::image
{
	// A seeded procedural texture (CONTEXT.md, "Material"): value noise over four octaves with soft
	// blobs on top, so a feature detector finds stable extrema at several scales. It is a function of
	// position on a surface, in metres, defined everywhere and the same on every platform, so a test
	// renderer can sample it per ray and a GPU renderer can bake it once; there is one implementation
	// either way. Lifted from M9's synthetic scene (libs/camera/test/texturedscene.h), which still
	// samples it per ray and draws the same footage it always drew.
	struct ProceduralTexture
	{
		std::uint64_t seed = 1;
		double cell = 0.04; // metres: the finest noise octave; blobs sit on a lattice five times coarser
	};

	// The texture's grey level, 0 to 255, at (u, v) metres on its surface.
	double proceduralGrey(const ProceduralTexture& texture, double u, double v);

	// The texture made into an image `width` x `height` texels, `texelsPerMetre` of them to a metre,
	// each its grey level at the texel's centre over 255: a linear Gray32F image in [0, 1], what a
	// renderer samples as reflectance. Texel (0, 0) covers [0, 1 / texelsPerMetre) on both axes.
	Image bakeProceduralTexture(const ProceduralTexture& texture, int width, int height, double texelsPerMetre);
} // namespace lain::image
