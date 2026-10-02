// The facades with NO backend registered. Its own executable, because a core::Factory only grows: in
// test-camera, a stand-in registered by another case would already be there.

#include "testboard.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/board.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/media/framesequence.h>
#include <lain/media/framesource.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace lain;
using namespace lain::camera::board;
using namespace lain::camera::testing;
namespace calibration = lain::camera::calibration;

namespace
{
	class StillSource : public media::FrameSource
	{
	public:
		StillSource()
			: FrameSource{core::Uri{"/still"}, spec(), 3}
		{
		}
		static media::FrameSpec spec()
		{
			media::FrameSpec s;
			s.extent = {64, 48};
			s.pixelFormat = image::PixelFormat::Gray8;
			return s;
		}

	protected:
		image::Image decodeFrame(std::size_t) const override { return image::Image{64, 48, image::PixelFormat::Gray8}; }
	};
} // namespace

TEST_CASE("with no backend, detect reports the missing capability as a failed report", "[camera][board]")
{
	REQUIRE_FALSE(canDetect());
	const DetectionReport report = detect(image::Image{64, 48, image::PixelFormat::Gray8}, {}, specification());
	CHECK(report.status == DetectionStatus::Failed);
	CHECK_FALSE(report.observation.has_value());
	REQUIRE(report.rejections.size() == 1);
	CHECK(report.rejections[0].reason == Rejection::NoBackend);
	CHECK(report.stats.cornersExpected == 24);
}

TEST_CASE("with no backend, render says the capability is missing", "[camera][board]")
{
	REQUIRE_FALSE(canRender());
	const RenderResult result = render(pattern());
	CHECK_FALSE(result.rendering.has_value());
	REQUIRE(result.diagnostics.size() == 1);
	CHECK(result.diagnostics[0].problem == RenderProblem::NoBackend);
	// The text a gui shows on the node (NodeEvaluation::fail), so it has to say what to do about it.
	CHECK(result.diagnostics[0].detail.find("no board renderer") != std::string::npos);
	CHECK(result.diagnostics[0].detail.find("LAIN_CAMERA_OPENCV") != std::string::npos);
}

TEST_CASE("with no backend, calibration fails with the missing capability", "[camera][calibration]")
{
	const media::FrameSequence footage = media::FrameSequence::over(std::make_shared<StillSource>());
	const calibration::Report report = calibration::board::calibrate(footage, specification(), {});
	CHECK(report.status == calibration::CalibrationStatus::Failed);
	REQUIRE_FALSE(report.failures.empty());
	CHECK(report.failures[0].failure == calibration::Failure::NoDetector);
	CHECK_FALSE(calibration::canEstimate());
}
