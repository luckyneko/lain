// Fixed-camera board registration end to end through both real backends: three cameras look at one
// rendered board sweeping through their shared view, the OpenCV plugin detects it and solves each
// board pose, and the Ceres plugin refines the rig. Nothing here is a stand-in, so it is built only
// where both plugins are; the frames are drawn in plain C++ (syntheticfootage.h) as the calibration
// fixture's are.

#include "registration.h"
#include "syntheticfootage.h"
#include "testboard.h"

#include <lain/camera/board/rendering.h>
#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/registration/board.h>
#include <lain/media/framesource.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera;
namespace method = lain::camera::registration::board;

namespace
{
	constexpr std::size_t kFrames = 16;
	const synthetic::Pinhole kCamera{};

	// Camera c turned about the board's mean position (0.42 m ahead of camera 0) by `angle` about the
	// vertical: referenceFromCamera, with camera 0 the identity.
	math::RigidTransformd referenceFromCamera(double angle)
	{
		const math::Vec3d pivot{0, 0, 0.42};
		const math::Quatd turn = math::angleAxis(angle, math::Vec3d{0, 1, 0});
		return math::RigidTransformd{turn, pivot - turn * pivot};
	}

	// What one camera sees of the sweep, drawn when a frame is decoded.
	class RenderedSource : public media::FrameSource
	{
	public:
		RenderedSource(std::string uri, std::shared_ptr<const board::Rendering> rendering, board::Specification spec,
					   math::RigidTransformd cameraFromReference)
			: FrameSource{core::Uri{uri}, frameSpec(), kFrames}
			, m_rendering(std::move(rendering))
			, m_spec(std::move(spec))
			, m_cameraFromReference(cameraFromReference)
		{
		}

		static media::FrameSpec frameSpec()
		{
			media::FrameSpec s;
			s.extent = {kCamera.width, kCamera.height};
			s.pixelFormat = image::PixelFormat::Gray8;
			s.colorSpace = image::ColorSpace::sRGB;
			return s;
		}

	protected:
		image::Image decodeFrame(std::size_t ordinal) const override
		{
			const math::RigidTransformd referenceFromBoard = synthetic::sweepPose(m_spec, ordinal, kFrames);
			return synthetic::view(*m_rendering, m_spec, kCamera, m_cameraFromReference * referenceFromBoard);
		}

	private:
		std::shared_ptr<const board::Rendering> m_rendering;
		board::Specification m_spec;
		math::RigidTransformd m_cameraFromReference;
	};

	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const math::Quatd r = math::conjugate(a.rotation()) * b.rotation();
		return 2.0 * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::abs(r.w));
	}
} // namespace

TEST_CASE("three cameras rendered, detected and refined register to their truth", "[camera][registration]")
{
	fixture::ensureRegistered();
	const lain::testing::ThreadPool pool;
	const board::Specification spec = camera::testing::specification();
	const board::RenderResult rendered = board::render(spec.pattern(), board::RenderRequest{60, 20});
	REQUIRE(rendered.rendering.has_value());
	const auto rendering = std::make_shared<const board::Rendering>(*rendered.rendering);

	CameraModelParameters p;
	p.image = {std::uint32_t(kCamera.width), std::uint32_t(kCamera.height)};
	p.intrinsics = {kCamera.fx, kCamera.fy, kCamera.cx, kCamera.cy};
	const CameraModel model = *CameraModel::create(p).model;

	const std::vector<double> angles{0.0, -0.25, 0.25};
	std::vector<method::RigFootage> cameras;
	std::vector<capture::CameraFootage> byPosition;
	for (std::size_t c = 0; c < angles.size(); ++c)
	{
		const std::string name = "cam" + std::to_string(c);
		const media::FrameSequence footage = media::FrameSequence::over(
			std::make_shared<RenderedSource>("/rendered/" + name, rendering, spec, referenceFromCamera(angles[c]).inverse()));
		cameras.push_back({capture::CameraIdentity{name}, model, footage});
		byPosition.push_back({capture::CameraIdentity{name}, footage});
	}
	const capture::GroupingResult grouping = capture::groupsByPosition(byPosition);
	REQUIRE(grouping.groups.size() == kFrames);

	registration::Request request;
	request.reference = capture::CameraIdentity{"cam0"};
	request.resamples = 4;
	const registration::Report report = method::registerCameras(cameras, grouping.groups, spec, request);
	INFO(report.toString());
	REQUIRE(report.status == registration::RegistrationStatus::Succeeded);
	CHECK(report.reproducibility.detector.backend == "opencv");
	CHECK(report.reproducibility.poseSolver.backend == "opencv");
	CHECK(report.reproducibility.refiner.backend == "ceres");
	CHECK(report.diagnostics.groupsUsable == kFrames);

	// Measured: the worst camera 0.28 mrad and 0.15 mm from the truth, and a held-out transfer of
	// 0.22 mrad, Ready.
	for (std::size_t c = 0; c < angles.size(); ++c)
	{
		CAPTURE(c);
		const math::RigidTransformd truth = referenceFromCamera(angles[c]);
		const math::RigidTransformd found = *report.referenceFromCamera(capture::CameraIdentity{"cam" + std::to_string(c)});
		CHECK(rotationBetween(found, truth) < 0.001);
		CHECK(math::length(found.translation() - truth.translation()) < 0.001);
	}
	const auto* held = std::get_if<registration::HeldOutEvidence>(&report.heldOut);
	REQUIRE(held != nullptr);
	CHECK(held->rmsAngle < 0.001);
	CHECK(report.verdict == Verdict::Ready);
}
