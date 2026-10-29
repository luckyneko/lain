#pragma once

#include <lain/image/image.h>

#include <opencv2/core.hpp>

// Preparing a lain image for an OpenCV search, and mapping what the search found back to source
// pixels: private to the plugin, so the board detector and the feature extractor read an image the
// same way and answer in the same pixels.
namespace lain::camera::opencv
{
	// An 8-bit single-channel picture of the image to look for features in. It is an INTENSITY
	// image, not a luminance measurement, so any colour space serves: 8-bit formats are read as they
	// are, and wider ones are brought to 8 bits first by lain's own conversion. Empty when the image
	// is unusable. An 8-bit grey source is viewed, not copied, so the result must not outlive it.
	cv::Mat intensity(const image::Image& source);

	// The image a search actually ran on, and the factors actually used. Rounding to whole pixels
	// makes them differ slightly from the request and from each other.
	struct Searched
	{
		cv::Mat image;
		double sx = 1.0;
		double sy = 1.0;

		bool native() const { return sx == 1.0 && sy == 1.0; }
	};

	// `grey` reduced by `scale` (INTER_AREA), or `grey` itself when the scale leaves it whole.
	Searched searchedAt(const cv::Mat& grey, double scale);

	// A pixel of an image resized by (sx, sy) back to source pixels. Pixel centres, not corners,
	// correspond: the centre of reduced pixel x sits at (x + 0.5) / sx - 0.5 in the source.
	inline cv::Point2f toSource(const cv::Point2f& p, double sx, double sy)
	{
		return {float((p.x + 0.5) / sx - 0.5), float((p.y + 0.5) / sy - 0.5)};
	}
} // namespace lain::camera::opencv
