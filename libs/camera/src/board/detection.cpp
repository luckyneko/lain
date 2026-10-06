#include "lain/camera/board/detection.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>

namespace lain::camera::board
{
	std::string DetectionReport::toString() const
	{
		std::string text = lain::string::format("{}: {} of {} corners, {} markers", meta::enums::name(status),
												stats.cornersFound, stats.cornersExpected, stats.markersFound);
		if (status == DetectionStatus::Failed && !rejections.empty())
			text += lain::string::format(" ({})", rejections.front().detail);
		return text;
	}

	core::Factory<Detector>& detectorRegistry()
	{
		static core::Factory<Detector> registry;
		return registry;
	}

	bool canDetect()
	{
		return !detectorRegistry().keys().empty();
	}

	DetectionReport detect(const image::Image& image, const media::FrameRef& frame, const Specification& board,
						   const DetectionRequest& request)
	{
		const core::Time start = core::Time::now();
		const std::vector<std::string> backends = detectorRegistry().keys();

		DetectionReport report;
		if (backends.empty())
		{
			report.rejections.push_back({Rejection::NoBackend, "this build has no board detector "
															   "(configure with -DLAIN_CAMERA_OPENCV=ON)"});
		}
		else
		{
			const std::unique_ptr<Detector> detector = detectorRegistry().create(backends.front());
			const ImageGeometry geometry{std::uint32_t(std::max(image.width(), 0)), std::uint32_t(std::max(image.height(), 0))};
			report = detector->detect(image, frame, board, request, resolveScale(request.scale, geometry));
		}

		// What every backend reports the same way, whatever it filled in.
		report.request = request;
		report.stats.cornersExpected = board.pattern().cornerCount();
		if (report.status == DetectionStatus::Failed)
			report.observation.reset();
		else if (!report.observation)
			report.status = DetectionStatus::Failed; // a success with nothing to show is not one
		if (report.observation)
		{
			std::vector<FeatureObservation>& features = report.observation->features;
			std::sort(features.begin(), features.end(),
					  [](const FeatureObservation& a, const FeatureObservation& b)
					  { return a.id < b.id; });
		}
		report.elapsed = core::Time::now() - start;
		return report;
	}
} // namespace lain::camera::board
