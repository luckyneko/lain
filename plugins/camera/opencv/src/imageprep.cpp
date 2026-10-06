#include "imageprep.h"

#include <lain/image/convert.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lain::camera::opencv
{
	cv::Mat intensity(const image::Image& source)
	{
		if (!source.valid())
			return {};
		const int w = source.width();
		const int h = source.height();
		// Read-only views over lain's tightly packed pixels; nothing below writes through them.
		auto* data = const_cast<std::uint8_t*>(source.data());
		cv::Mat grey;
		switch (source.pixelFormat())
		{
			case image::PixelFormat::Gray8:
				return cv::Mat(h, w, CV_8UC1, data);
			case image::PixelFormat::GrayAlpha8:
				cv::extractChannel(cv::Mat(h, w, CV_8UC2, data), grey, 0);
				return grey;
			case image::PixelFormat::RGB8:
				cv::cvtColor(cv::Mat(h, w, CV_8UC3, data), grey, cv::COLOR_RGB2GRAY);
				return grey;
			case image::PixelFormat::RGBA8:
				cv::cvtColor(cv::Mat(h, w, CV_8UC4, data), grey, cv::COLOR_RGBA2GRAY);
				return grey;
			case image::PixelFormat::Gray16:
			case image::PixelFormat::Gray32F:
			case image::PixelFormat::GrayAlpha16:
			case image::PixelFormat::GrayAlpha32F:
			{
				const image::Image narrowed = image::convert(source, image::PixelFormat::Gray8);
				return narrowed.valid() ? intensity(narrowed).clone() : cv::Mat{};
			}
			case image::PixelFormat::RGB16:
			case image::PixelFormat::RGB32F:
			case image::PixelFormat::RGBA16:
			case image::PixelFormat::RGBA32F:
			{
				// Narrowed per channel, not reduced to grey: lain's reduction to grey is a luminance and
				// rightly insists on linear light, which a picture to find features in does not need.
				const image::Image narrowed = image::convert(source, image::PixelFormat::RGB8);
				return narrowed.valid() ? intensity(narrowed).clone() : cv::Mat{};
			}
		}
		return {};
	}

	Searched searchedAt(const cv::Mat& grey, double scale)
	{
		Searched out;
		out.image = grey;
		if (scale < 1.0)
		{
			const int rw = std::max(1, int(std::lround(grey.cols * scale)));
			const int rh = std::max(1, int(std::lround(grey.rows * scale)));
			if (rw < grey.cols || rh < grey.rows)
			{
				cv::resize(grey, out.image, cv::Size(rw, rh), 0, 0, cv::INTER_AREA);
				out.sx = double(rw) / grey.cols;
				out.sy = double(rh) / grey.rows;
			}
		}
		return out;
	}
} // namespace lain::camera::opencv
