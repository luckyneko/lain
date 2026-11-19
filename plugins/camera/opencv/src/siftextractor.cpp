#include "siftextractor.h"

#include "imageprep.h"

#include <opencv2/core/utility.hpp>
#include <opencv2/features2d.hpp>

#include <cstring>
#include <vector>

namespace lain::camera::opencv
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static constexpr double kPi = 3.141592653589793;

	// --- SiftExtractor ------------------------------------------------------------

	Provenance SiftExtractor::provenance() const
	{
		return {"opencv", cv::getVersionString()};
	}

	std::optional<feature::Features> SiftExtractor::extract(const image::Image& image, double scale) const
	{
		const cv::Mat grey = intensity(image);
		if (grey.empty())
			return std::nullopt;
		try
		{
			const Searched searched = searchedAt(grey, scale);
			// Lowe's parameters, with CV_8U descriptors (a quarter of CV_32F's size) and precise
			// upscaling, which OpenCV's header says prevents a localisation bias in the doubled first
			// octave: features land where they are, which every pose and track measured from them
			// relies on. That doubled octave, a float pyramid at twice the searched size, is also what
			// a search costs in memory: 489 MB for a 1920 x 1080 search (measured, test_features.cpp).
			const cv::Ptr<cv::SIFT> sift = cv::SIFT::create(0, 3, 0.04, 10, 1.6, CV_8U, true);
			std::vector<cv::KeyPoint> keypoints;
			cv::Mat descriptors;
			sift->detectAndCompute(searched.image, cv::noArray(), keypoints, descriptors);

			feature::Features out;
			out.kind = "sift";
			out.descriptorBytes = 128;
			if (keypoints.empty())
				return out;
			if (descriptors.type() != CV_8U || descriptors.cols != 128 || descriptors.rows != int(keypoints.size()))
				return std::nullopt;
			out.keypoints.reserve(keypoints.size());
			out.descriptors.resize(keypoints.size() * 128);
			for (std::size_t i = 0; i < keypoints.size(); ++i)
			{
				const cv::KeyPoint& k = keypoints[i];
				const cv::Point2f p = toSource(k.pt, searched.sx, searched.sy);
				out.keypoints.push_back({{p.x, p.y}, double(k.size) / searched.sx, double(k.angle) * kPi / 180.0, double(k.response)});
				std::memcpy(out.descriptors.data() + i * 128, descriptors.ptr<std::uint8_t>(int(i)), 128);
			}
			return out;
		}
		catch (const cv::Exception&)
		{
			// The seam has one way to say this image cannot be searched.
			return std::nullopt;
		}
	}
} // namespace lain::camera::opencv
