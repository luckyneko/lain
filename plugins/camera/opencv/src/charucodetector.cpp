#include "charucodetector.h"

#include "charucoboard.h"

#include <lain/image/convert.h>

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace lain::camera::opencv
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// An 8-bit single-channel picture of the image to look for features in. It is an INTENSITY
	// image, not a luminance measurement, so any colour space serves: 8-bit formats are read as they
	// are, and wider ones are brought to 8 bits first by lain's own conversion. Empty when the image
	// is unusable.
	static cv::Mat intensity(const image::Image& source)
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
				// rightly insists on linear light, which a picture to find corners in does not need.
				const image::Image narrowed = image::convert(source, image::PixelFormat::RGB8);
				return narrowed.valid() ? intensity(narrowed).clone() : cv::Mat{};
			}
		}
		return {};
	}

	// A pixel of an image resized by (sx, sy) back to source pixels. Pixel centres, not corners,
	// correspond: the centre of reduced pixel x sits at (x + 0.5) / sx - 0.5 in the source.
	static cv::Point2f toSource(const cv::Point2f& p, double sx, double sy)
	{
		return {float((p.x + 0.5) / sx - 0.5), float((p.y + 0.5) / sy - 0.5)};
	}

	// Half the refinement window, in source pixels. It must stay inside the GAP between a chessboard
	// corner and the nearest marker, (1 - markerToSquare) / 2 of a square: a marker's edges inside
	// the window pull the corner towards them. The first cut used a quarter of the corner spacing,
	// which reaches past that gap for any marker ratio above 0.5, and refinement then made a
	// half-scale detection worse (0.80 px) than not refining at all. OpenCV sizes its own ChArUco
	// refinement window from marker distance for the same reason. The smallest corner spacing
	// stands in for the square's side, which covers foreshortening.
	static int refinementHalfWindow(const std::vector<cv::Point2f>& corners, double markerToSquare)
	{
		double spacing = std::numeric_limits<double>::infinity();
		for (std::size_t i = 0; i < corners.size(); ++i)
		{
			for (std::size_t j = i + 1; j < corners.size(); ++j)
				spacing = std::min(spacing, double(cv::norm(corners[i] - corners[j])));
		}
		if (!std::isfinite(spacing))
			return 2;
		const double gap = spacing * (1.0 - markerToSquare) / 2.0;
		return std::max(2, int(0.8 * gap));
	}

	// The detection itself; detect() below turns an OpenCV exception into a report.
	static board::DetectionReport detectBoard(const image::Image& image, const media::FrameRef& frame,
											  const board::Specification& specification,
											  const board::DetectionRequest& request, double scale)
	{
		board::DetectionReport report;
		report.provenance = {"opencv", cv::getVersionString()};

		const cv::Mat grey = intensity(image);
		if (grey.empty())
		{
			report.rejections.push_back({board::Rejection::UnusableImage, "the image is empty or cannot be read"});
			return report;
		}

		// Search a reduced image when asked, and record the factors actually used: rounding to whole
		// pixels makes them differ slightly from the request and from each other.
		cv::Mat searched = grey;
		double sx = 1.0, sy = 1.0;
		if (scale < 1.0)
		{
			const int rw = std::max(1, int(std::lround(grey.cols * scale)));
			const int rh = std::max(1, int(std::lround(grey.rows * scale)));
			if (rw < grey.cols || rh < grey.rows)
			{
				cv::resize(grey, searched, cv::Size(rw, rh), 0, 0, cv::INTER_AREA);
				sx = double(rw) / grey.cols;
				sy = double(rh) / grey.rows;
			}
		}
		report.transform = {sx, sy};
		const bool native = sx == 1.0 && sy == 1.0;

		const board::Pattern& pattern = specification.pattern();
		const cv::aruco::CharucoBoard cvBoard =
			toCharucoBoard(pattern, float(specification.instance().squareLength.value.metres()));

		std::vector<std::vector<cv::Point2f>> markerCorners, rejected;
		std::vector<int> markerIds;
		cv::aruco::ArucoDetector(cvBoard.getDictionary()).detectMarkers(searched, markerCorners, markerIds, rejected);

		const int firstId = int(pattern.parameters().firstMarkerId);
		const int lastId = firstId + int(pattern.markerCount()) - 1;
		report.stats.markersFound = std::uint32_t(
			std::count_if(markerIds.begin(), markerIds.end(), [&](int id)
						  { return id >= firstId && id <= lastId; }));
		report.stats.markersRejected = std::uint32_t(rejected.size());

		std::vector<cv::Point2f> corners;
		std::vector<int> cornerIds;
		if (report.stats.markersFound > 0)
			cv::aruco::CharucoDetector(cvBoard).detectBoard(searched, corners, cornerIds, markerCorners, markerIds);
		for (cv::Point2f& corner : corners)
			corner = toSource(corner, sx, sy);

		// OpenCV already refines interpolated corners in the image it searched. When that image was
		// reduced, refine again against source pixels, around the mapped positions only.
		if (native)
			report.refinement = board::RefinementResolution::Native;
		else if (request.refineAtNativeResolution && !corners.empty())
		{
			const int half = refinementHalfWindow(corners, pattern.parameters().markerToSquare);
			cv::cornerSubPix(grey, corners, cv::Size(half, half), cv::Size(-1, -1),
							 cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 40, 0.001));
			report.refinement = board::RefinementResolution::Native;
		}
		else if (!corners.empty())
			report.refinement = board::RefinementResolution::DetectionScale;
		report.stats.cornersFound = std::uint32_t(corners.size());

		if (request.detail == board::DetailLevel::Detailed)
		{
			board::DetectionEvidence evidence;
			for (const std::vector<cv::Point2f>& quad : rejected)
			{
				std::array<math::Vec2d, 4> mapped{};
				for (std::size_t i = 0; i < 4 && i < quad.size(); ++i)
				{
					const cv::Point2f p = toSource(quad[i], sx, sy);
					mapped[i] = {p.x, p.y};
				}
				evidence.rejectedMarkers.push_back(mapped);
			}
			report.evidence = std::move(evidence);
		}

		if (report.stats.markersFound == 0)
		{
			report.rejections.push_back({board::Rejection::NoMarkers, "no marker with ids " + std::to_string(firstId) + "-" +
																		  std::to_string(lastId) + " of " +
																		  std::string(board::name(pattern.parameters().dictionary)) +
																		  " was found"});
			return report;
		}
		if (corners.size() < request.minimumCorners)
		{
			report.rejections.push_back({board::Rejection::TooFewCorners,
										 std::to_string(corners.size()) + " corners found; at least " +
											 std::to_string(request.minimumCorners) + " are needed"});
			return report;
		}

		board::Observation observation;
		observation.frame = frame;
		observation.image = {std::uint32_t(grey.cols), std::uint32_t(grey.rows)};
		observation.pattern = pattern.fingerprint();
		for (std::size_t i = 0; i < corners.size(); ++i)
			observation.features.push_back({std::uint32_t(cornerIds[i]), {corners[i].x, corners[i].y}, std::nullopt});
		report.observation = std::move(observation);
		report.status =
			corners.size() == pattern.cornerCount() ? board::DetectionStatus::Detected : board::DetectionStatus::Partial;
		return report;
	}

	// --- CharucoDetector ----------------------------------------------------------

	board::DetectionReport CharucoDetector::detect(const image::Image& image, const media::FrameRef& frame,
												   const board::Specification& specification,
												   const board::DetectionRequest& request, double scale) const
	{
		// A detection always ends in a report (ADR-0016), so an OpenCV exception becomes a failed
		// one rather than escaping into whatever called the facade.
		try
		{
			return detectBoard(image, frame, specification, request, scale);
		}
		catch (const cv::Exception& e)
		{
			board::DetectionReport report;
			report.provenance = {"opencv", cv::getVersionString()};
			report.rejections.push_back({board::Rejection::UnusableImage, std::string("OpenCV refused the frame: ") + e.what()});
			return report;
		}
	}
} // namespace lain::camera::opencv
