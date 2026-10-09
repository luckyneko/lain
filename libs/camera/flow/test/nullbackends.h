#pragma once

// A backend of every camera seam that answers nothing, for the tests that stage which node kinds are
// offered: each registry learns of a backend, and what it would answer is beside the point. Register
// one as "null" into its process-wide registry.

#include <lain/camera/board/detection.h>
#include <lain/camera/board/pose.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/feature/geometry.h>
#include <lain/camera/feature/matching.h>
#include <lain/camera/registration/refiner.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace lain::camera::testing::null
{
	class NullRenderer : public camera::board::Renderer
	{
	public:
		image::Image raster(const camera::board::Pattern&, const camera::board::RenderRequest&) const override
		{
			return {};
		}
	};

	class NullDetector : public camera::board::Detector
	{
	public:
		camera::board::DetectionReport detect(const image::Image&, const media::FrameRef&,
											  const camera::board::Specification&, const camera::board::DetectionRequest&,
											  double) const override
		{
			return {};
		}
	};

	class NullEstimator : public camera::calibration::Estimator
	{
	public:
		camera::Provenance provenance() const override { return {"null", "1"}; }
		bool canEstimate(camera::DistortionModel) const override { return true; }
		camera::calibration::Estimate estimate(const camera::ImageGeometry&,
											   const std::vector<camera::board::Observation>&,
											   const camera::board::Specification&, camera::DistortionModel,
											   const std::optional<camera::CameraModelParameters>&) const override
		{
			return {};
		}
	};

	class NullPoseSolver : public camera::board::PoseSolver
	{
	public:
		camera::Provenance provenance() const override { return {"null", "1"}; }
		std::vector<math::RigidTransformd> solve(const camera::CameraModel&, const camera::board::Specification&,
												 const camera::board::Observation&) const override
		{
			return {};
		}
	};

	class NullRefiner : public camera::registration::Refiner
	{
	public:
		camera::Provenance provenance() const override { return {"null", "1"}; }
		camera::registration::Solution refine(const camera::registration::Problem&) const override { return {}; }
	};

	class NullExtractor : public camera::feature::Extractor
	{
	public:
		camera::Provenance provenance() const override { return {"null", "1"}; }
		std::optional<camera::feature::Features> extract(const image::Image&, double) const override { return {}; }
	};

	class NullMatcher : public camera::feature::Matcher
	{
	public:
		camera::Provenance provenance() const override { return {"null", "1"}; }
		bool accepts(std::string_view) const override { return true; }
		bool supports(camera::feature::MatchSearch) const override { return true; }
		std::vector<std::array<camera::feature::Neighbour, 2>> nearest(const camera::feature::Features&,
																	   const camera::feature::Features&,
																	   camera::feature::MatchSearch) const override
		{
			return {};
		}
	};

	class NullGeometry : public camera::feature::GeometrySolver
	{
	public:
		camera::Provenance provenance() const override { return {"null", "1"}; }
		std::vector<math::RigidTransformd> relativePoses(const std::vector<camera::feature::RayPair>&, double,
														 std::uint64_t) const override
		{
			return {};
		}
		std::vector<math::RigidTransformd> absolutePoses(const std::vector<camera::feature::PointRay>&, double,
														 std::uint64_t) const override
		{
			return {};
		}
	};
} // namespace lain::camera::testing::null
