#include "charucorenderer.h"

#include "charucoboard.h"

#include <cstring>

namespace lain::camera::opencv
{
	image::Image CharucoRenderer::raster(const board::Pattern& pattern, const board::RenderRequest& request) const
	{
		const board::PatternParameters& p = pattern.parameters();
		const int margin = int(request.marginPixels);
		const cv::Size size(int(p.squaresX * request.pixelsPerSquare) + 2 * margin,
							int(p.squaresY * request.pixelsPerSquare) + 2 * margin);

		cv::Mat drawn;
		try
		{
			toCharucoBoard(pattern, 1.0f).generateImage(size, drawn, margin, 1);
		}
		catch (const cv::Exception&)
		{
			return {}; // an invalid image is the refusal render() already reports
		}
		if (drawn.type() != CV_8UC1 || drawn.size() != size)
			return {};

		// Black and white only, so any transfer curve reads it the same; sRGB is the honest tag for
		// something meant to be printed and looked at.
		image::Image out{size.width, size.height, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		for (int row = 0; row < size.height; ++row)
			std::memcpy(out.data() + std::size_t(row) * std::size_t(size.width), drawn.ptr<std::uint8_t>(row),
						std::size_t(size.width));
		return out;
	}
} // namespace lain::camera::opencv
