#pragma once

// A stand-in board renderer for the tests that need a board drawn whatever plugins the build has: black
// squares inside a white margin, at the size asked for, or nothing when asked to fail. A renderer
// registry only grows, so an executable that registers it can no longer test a board with none.

#include <lain/camera/board/rendering.h>
#include <lain/image/image.h>

#include <cstddef>

class StandInBoard : public lain::camera::board::Renderer
{
public:
	static inline bool fail = false;

	lain::image::Image raster(const lain::camera::board::Pattern& pattern, const lain::camera::board::RenderRequest& request) const override
	{
		if (fail)
			return {};
		const lain::camera::board::PatternParameters& p = pattern.parameters();
		const int width = int(p.squaresX * request.pixelsPerSquare + 2 * request.marginPixels);
		const int height = int(p.squaresY * request.pixelsPerSquare + 2 * request.marginPixels);
		lain::image::Image out(width, height, lain::image::PixelFormat::Gray8, lain::image::ColorSpace::sRGB);
		const int margin = int(request.marginPixels);
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const bool inside = x >= margin && x < width - margin && y >= margin && y < height - margin;
				out.data()[std::size_t(y) * std::size_t(width) + std::size_t(x)] = inside ? 0 : 255;
			}
		}
		return out;
	}
};

inline void registerStandIn()
{
	static const bool once = []
	{
		lain::camera::board::rendererRegistry().registerType<StandInBoard>("stand-in");
		return true;
	}();
	(void)once;
}
