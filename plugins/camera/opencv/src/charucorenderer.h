#pragma once

#include <lain/camera/board/rendering.h>

namespace lain::camera::opencv
{
	// Draws a ChArUco pattern with OpenCV's own generator, so a board lain renders is exactly what
	// the detector expects to find.
	class CharucoRenderer : public board::Renderer
	{
	public:
		image::Image raster(const board::Pattern& pattern, const board::RenderRequest& request) const override;
	};
} // namespace lain::camera::opencv
