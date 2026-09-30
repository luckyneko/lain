#include "lain/camera/board/detection.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>

namespace lain::camera::board
{
	double resolveScale(const ScalePolicy& policy, const ImageGeometry& image)
	{
		return std::visit(
			[&image](const auto& p) -> double
			{
				using P = std::decay_t<decltype(p)>;
				if constexpr (std::is_same_v<P, NativeScale>)
				{
					(void)p;
					return 1.0;
				}
				else if constexpr (std::is_same_v<P, ScaleFactor>)
				{
					// Zero, negative or non-finite asks for nothing coherent; native is the one
					// answer that cannot lose a board.
					return std::isfinite(p.factor) && p.factor > 0 ? std::min(p.factor, 1.0) : 1.0;
				}
				else
				{
					const std::uint32_t longest = std::max(image.width, image.height);
					if (p.pixels == 0 || longest == 0)
						return 1.0;
					return std::min(1.0, double(p.pixels) / double(longest));
				}
			},
			policy);
	}

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
