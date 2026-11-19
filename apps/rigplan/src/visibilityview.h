#pragma once

#include <lain/image/image.h>
#include <lain/rigplan/visibility.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rigplan
{
	// How the visibility pane shows a map: a picture of it, its legend, and the cell under the pointer.
	//
	// A count is ordinal, so it takes one hue, darkest for the fewest cameras and lightest for the most,
	// as a ramp reads on a dark surface (the gui's): the blue ramp of steps 600 to 100. No camera is the
	// dark surface itself, bare floor. One camera is its own step, since registration needs shared views
	// and a cell one camera sees is still a blind spot for it; six or more share the lightest. A count is
	// never colour alone: the legend writes it, and the hover says it and names the cameras.

	// What one colour of the legend counts: `count` cameras, or that many or more for the last.
	struct LegendEntry
	{
		std::size_t count = 0;
		bool orMore = false;
		std::array<std::uint8_t, 3> colour{}; // sRGB
	};

	// The legend, fewest cameras first.
	const std::vector<LegendEntry>& visibilityLegend();

	// The colour a cell seen by `count` cameras is drawn in.
	std::array<std::uint8_t, 3> visibilityColour(std::size_t count);

	// The map as a picture, as a floor plan is drawn: the plan's +Y (north) up and its +X (east) right,
	// each cell `scale` x `scale` texels of its colour. RGBA8, sRGB, opaque. Invalid for a map with no
	// cells. The gui filters a texture linearly, so a cell a texel across, magnified, would blend into
	// its neighbours' colours, which the legend does not have; drawn about as many texels as it is shown
	// pixels, the filtering softens only the pixel at a cell's edge.
	lain::image::Image visibilityPicture(const lain::rigplan::VisibilityMap& map, int scale = 1);

	// The scale a picture of `grid` drawn `pixels` across its longer side wants: about a texel a pixel,
	// with no side past 2048 texels, which a pane resized through a few scales remakes in milliseconds.
	int visibilityPictureScale(const lain::rigplan::VisibilityGrid& grid, double pixels);

	// The cell (column, row) under point (x, y) of that picture, each in [0, 1) from its top-left corner,
	// or none outside it.
	std::optional<std::pair<int, int>> cellUnder(const lain::rigplan::VisibilityGrid& grid, double x, double y);
} // namespace rigplan
