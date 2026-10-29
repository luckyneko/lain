// Targetless registration through the Ceres refiner (registration::targetless): its evidence, held-out
// tracks predicted member by member with every camera held (registration::validate); a noisy rig; a
// wrong match named; a landmark behind a camera kept out of the solve; and, in Release, a rig of 100
// cameras. The stand-in scene is libs/camera/test's (syntheticscene.h). Every tolerance is a
// measurement, written beside its check.

#include "syntheticscene.h"

#include <lain/camera/ceres/register.h>
#include <lain/camera/registration/targetless.h>
#include <lain/camera/registration/validation.h>
#include <lain/core/time.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <set>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
namespace scene = lain::camera::testing::scene;

namespace
{
	void ensureCeres()
	{
		static const bool once = []
		{
			lain::camera::ceres::registerBackend();
			return true;
		}();
		(void)once;
	}

	std::vector<RigCamera> rig()
	{
		std::vector<RigCamera> out;
		for (std::size_t c = 0; c < scene::scene().referenceFromCamera.size(); ++c)
			out.push_back({capture::CameraIdentity{scene::identityOf(c)}, scene::model()});
		return out;
	}

	Report truthReport()
	{
		Report report;
		report.status = RegistrationStatus::Succeeded;
		const std::vector<math::RigidTransformd>& truth = scene::scene().referenceFromCamera;
		for (std::size_t c = 0; c < truth.size(); ++c)
			report.cameras.push_back({capture::CameraIdentity{scene::identityOf(c)}, truth[0].inverse() * truth[c]});
		return report;
	}
} // namespace

TEST_CASE("held-out tracks of a true registration are predicted to within their noise", "[camera][ceres][targetless]")
{
	ensureCeres();
	scene::reset(4, 200);
	const double sigma = 0.3; // pixels, per axis
	const feature::TrackSet set = scene::trackSetOf(sigma, 11);
	std::vector<std::string> ids;
	std::size_t members = 0;
	for (const feature::Track& t : set.tracks)
	{
		ids.push_back(t.identity);
		members += t.observations.size();
	}
	const auto result = validate(rig(), truthReport(), set, ids);
	REQUIRE(std::holds_alternative<HeldOutEvidence>(result));
	const HeldOutEvidence& e = std::get<HeldOutEvidence>(result);
	CHECK(e.predictions == members);
	CHECK(e.unpredicted == 0);
	// A member's observed pixel is off by the noise in two axes, and its prediction, from three noisy
	// others, adds its own error: the RMS pixel residual sits above sqrt(2) * sigma. Measured 0.587 px,
	// 1.38 times that, at 1.12 mrad RMS, the worst member 1.80 px out.
	CHECK(e.rmsPixels > std::sqrt(2.0) * sigma);
	CHECK(e.rmsPixels < 2 * std::sqrt(2.0) * sigma);
}

namespace
{
	void registerScene()
	{
		ensureCeres();
		scene::registerStandIns();
	}

	// The worst camera rotation against the truth relative to camera 0, and the worst centre, once the
	// registration's scale is taken out by least squares over the centres, over the median depth.
	struct Errors
	{
		double rotation = 0, centre = 0;
	};
	Errors errorsOf(const Report& report)
	{
		const std::vector<math::RigidTransformd>& truth = scene::scene().referenceFromCamera;
		double along = 0, squared = 0;
		for (std::size_t c = 0; c < truth.size(); ++c)
		{
			const math::Vec3d actual = (truth[0].inverse() * truth[c]).translation();
			along += math::dot(report.cameras[c].referenceFromCamera.translation(), actual);
			squared += math::dot(actual, actual);
		}
		const double scale = along / squared;
		Errors out;
		for (std::size_t c = 0; c < truth.size(); ++c)
		{
			const math::RigidTransformd expected = truth[0].inverse() * truth[c];
			const math::RigidTransformd& actual = report.cameras[c].referenceFromCamera;
			const math::Quatd r = math::conjugate(expected.rotation()) * actual.rotation();
			out.rotation = std::max(out.rotation, 2.0 * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::abs(r.w)));
			out.centre = std::max(out.centre, math::length(actual.translation() - expected.translation() * scale) /
												  report.diagnostics.medianDepth);
		}
		return out;
	}
} // namespace

TEST_CASE("a noisy rig registers to within its noise, Ready", "[camera][ceres][targetless]")
{
	registerScene();
	scene::reset(4, 400);
	// The noise extraction's covariance states for a stand-in feature: 3.4% of its size, 4 px.
	const Report report = targetless::registerCameras(rig(), scene::trackSetOf(0.034 * 4, 3), Request{});
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	const Errors errors = errorsOf(report);
	// Measured: the worst camera 0.454 mrad out, the worst centre 0.039% of the median depth; held out
	// 0.551 mrad over 80 tracks; 6 outliers in 1280 observations, as a 3-sigma cut of Gaussian noise
	// leaves.
	CHECK(errors.rotation < 0.0006);
	CHECK(errors.centre < 0.002);
	CHECK(report.verdict == Verdict::Ready);
}

TEST_CASE("a wrong match is named as an outlier, and the rest of its track is not", "[camera][ceres][targetless]")
{
	registerScene();
	scene::reset(4, 300);
	feature::TrackSet set = scene::trackSetOf();
	feature::Track& wrong = set.tracks[0]; // the first track is fitted: the hold-out stride starts at the third
	wrong.observations[2].pixel += math::Vec2d{15.0, 0.0};
	const Report report = targetless::registerCameras(rig(), set, Request{});
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	REQUIRE(report.diagnostics.outliers.size() == 1);
	const Outlier& o = report.diagnostics.outliers.front();
	CHECK(o.unit == wrong.identity);
	CHECK(o.camera.value == scene::identityOf(wrong.observations[2].view));
	// Measured 110 standard deviations: 15 px against a 0.136 px sigma. Cauchy's loss still lets it pull
	// a little: the rig moves 1.2 microradians.
	CHECK(o.rmsWhitened > 100);
	CHECK(errorsOf(report).rotation < 1e-5);
}

TEST_CASE("a landmark behind a camera at the start is kept out of the solve, which then succeeds", "[camera][ceres][targetless]")
{
	registerScene();
	scene::reset(4, 200);
	scene::scene().referenceFromCamera[3] = scene::lookingAt(math::Vec3d{0.05, -0.1, -0.2}, math::Vec3d{0.0, 0.0, 3.0});
	scene::scene().renderedFrom = scene::scene().referenceFromCamera;
	feature::TrackSet set = scene::trackSetOf();
	feature::Track* wrong = nullptr;
	for (std::size_t u = 0; u < set.tracks.size() && !wrong; ++u)
	{
		const std::size_t l = std::stoul(set.tracks[u].identity.substr(5));
		const math::Vec3d inThree = scene::scene().referenceFromCamera[3].inverse().apply(scene::scene().landmarks[l].point);
		if (inThree.z < 0 && u % 5 != 2 && set.tracks[u].observations.back().view != 3)
			wrong = &set.tracks[u];
	}
	REQUIRE(wrong != nullptr);
	wrong->observations.push_back({3, math::Vec2d{320.0, 240.0}, 1, wrong->observations.front().covariance});
	const Report report = targetless::registerCameras(rig(), set, Request{});
	INFO(report.toString());
	// Left in, the landmark would project behind camera 3 at the start, which fails the whole solve.
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	CHECK(std::get<TargetlessDiagnostics>(report.diagnostics.method).behind == 1);
	CHECK(errorsOf(report).rotation < 1e-6);
}

TEST_CASE("a rig of 100 cameras and 50000 tracks registers", "[camera][ceres][targetless][scale]")
{
#ifndef NDEBUG
	SKIP("the scale test runs in Release builds: a Debug refinement is some 65 times slower");
#else
	registerScene();
	// A ring of 100 cameras 1 m from the origin, looking at it, and 50,000 landmarks in a cube of
	// half-side 0.25 m, each seen by the 8 cameras from its index on: the shape of test_landmarks.cpp's
	// refinement scale test, here through the whole method.
	scene::reset(100, 50000, 9);
	scene::Scene& s = scene::scene();
	for (std::size_t c = 0; c < 100; ++c)
	{
		const double a = 6.283185307179586 * double(c) / 100.0;
		s.referenceFromCamera[c] =
			scene::lookingAt(math::Vec3d{std::sin(a), -0.05 * double(c % 3), -std::cos(a)}, math::Vec3d{0.0});
	}
	s.renderedFrom = s.referenceFromCamera;
	for (std::size_t l = 0; l < 50000; ++l)
	{
		s.landmarks[l].point *= 0.25 / 1.0;
		s.landmarks[l].cameras.clear();
		for (std::size_t k = 0; k < 8; ++k)
			s.landmarks[l].cameras.insert((l + k) % 100);
	}
	const feature::TrackSet set = scene::trackSetOf(0.034 * 4, 5);
	REQUIRE(set.tracks.size() == 50000);

	const lain::testing::ThreadPool pool;
	const core::Time start = core::Time::now();
	const Report report = targetless::registerCameras(rig(), set, Request{});
	const double seconds = (core::Time::now() - start).seconds();
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	const Errors errors = errorsOf(report);
	// Measured on linux-x86_64, Release, four cores: 40 s in the method, 7.0 s of it the refinement, the
	// rest mostly the ten bootstrap registrations; 1.26 GB at peak for the whole test, its scene
	// included. The worst camera is 0.66 mrad out and the worst centre 0.057% of the median depth, a
	// ring's drift; held out 0.416 mrad over 10,000 tracks; 2,488 outliers in 320,000 observations.
	CHECK(seconds < 120);
	CHECK(errors.rotation < 0.001);
	CHECK(report.verdict == Verdict::Ready);
#endif
}
