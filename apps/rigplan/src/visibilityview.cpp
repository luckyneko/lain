#include "visibilityview.h"

#include <algorithm>
#include <cmath>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	const std::vector<LegendEntry>& visibilityLegend()
	{
		// The dataviz skill's sequential blue, steps 600, 500, 400, 300, 200 and 100, its OKLab lightness
		// rising by about 0.095 a step; the fewest-camera step clears 2.15:1 on the surface that stands for
		// none (WORK.md M15, *Sub-slice 9h built*).
		static const std::vector<LegendEntry> legend = {
			{0, false, {0x1a, 0x1a, 0x19}},
			{1, false, {0x18, 0x4f, 0x95}},
			{2, false, {0x25, 0x6a, 0xbf}},
			{3, false, {0x39, 0x87, 0xe5}},
			{4, false, {0x6d, 0xa7, 0xec}},
			{5, false, {0x9e, 0xc5, 0xf4}},
			{6, true, {0xcd, 0xe2, 0xfb}},
		};
		return legend;
	}

	std::array<std::uint8_t, 3> visibilityColour(std::size_t count)
	{
		const std::vector<LegendEntry>& legend = visibilityLegend();
		return legend[std::min(count, legend.size() - 1)].colour;
	}

	image::Image visibilityPicture(const rp::VisibilityMap& map, int scale)
	{
		const rp::VisibilityGrid& grid = map.grid();
		if (grid.cells() == 0 || scale < 1)
			return {};
		const int width = grid.columns * scale;
		const int height = grid.rows * scale;
		image::Image picture(width, height, image::PixelFormat::RGBA8, image::ColorSpace::sRGB, image::AlphaMode::Straight);
		std::uint8_t* out = picture.data();
		for (int y = 0; y < height; ++y)
		{
			const int row = grid.rows - 1 - y / scale; // the picture's top is the plan's north
			for (int x = 0; x < width; ++x)
			{
				const std::array<std::uint8_t, 3> colour = visibilityColour(map.count(x / scale, row));
				std::uint8_t* p = out + 4 * (std::size_t(y) * std::size_t(width) + std::size_t(x));
				p[0] = colour[0];
				p[1] = colour[1];
				p[2] = colour[2];
				p[3] = 255;
			}
		}
		return picture;
	}

	int visibilityPictureScale(const rp::VisibilityGrid& grid, double pixels)
	{
		const int longer = std::max(grid.columns, grid.rows);
		if (longer <= 0)
			return 1;
		const int wanted = int(std::ceil(pixels / longer));
		return std::clamp(wanted, 1, std::max(1, 2048 / longer));
	}

	std::optional<std::pair<int, int>> cellUnder(const rp::VisibilityGrid& grid, double x, double y)
	{
		if (!(x >= 0 && x < 1 && y >= 0 && y < 1) || grid.cells() == 0)
			return std::nullopt;
		const int column = std::min(int(std::floor(x * grid.columns)), grid.columns - 1);
		const int fromTop = std::min(int(std::floor(y * grid.rows)), grid.rows - 1);
		return std::make_pair(column, grid.rows - 1 - fromTop);
	}
} // namespace rigplan
