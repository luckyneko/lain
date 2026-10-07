// Targetless registration's evidence, over stand-ins answering from a known scene (syntheticscene.h)
// and a refiner that hands back its start (passthroughrefiner.h): held-out tracks predicted member by
// member (registration::validate). Every measured number is written beside its check.

#include "passthroughrefiner.h"
#include "syntheticscene.h"

#include <lain/camera/registration/validation.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
namespace scene = lain::camera::testing::scene;
namespace standin = lain::camera::testing;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

namespace
{
	void setUp(std::size_t cameras = 4, std::size_t landmarks = 60)
	{
		scene::registerStandIns();
		standin::registerPassThroughRefiner();
		scene::reset(cameras, landmarks);
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		standin::refinerScript().asked.clear();
	}

	std::vector<RigCamera> rig()
	{
		std::vector<RigCamera> out;
		for (std::size_t c = 0; c < scene::scene().referenceFromCamera.size(); ++c)
			out.push_back({capture::CameraIdentity{scene::identityOf(c)}, scene::model()});
		return out;
	}

	// A registration report of the truth, relative to `reference`: what a perfect registration says.
	Report truthReport(std::size_t reference = 0)
	{
		Report report;
		report.status = RegistrationStatus::Succeeded;
		const std::vector<math::RigidTransformd>& truth = scene::scene().referenceFromCamera;
		report.reference = capture::CameraIdentity{scene::identityOf(reference)};
		for (std::size_t c = 0; c < truth.size(); ++c)
			report.cameras.push_back({capture::CameraIdentity{scene::identityOf(c)}, truth[reference].inverse() * truth[c]});
		return report;
	}

	// Turn `camera`'s registered orientation by `angle` radians about `axis` (in the reference frame),
	// about its own centre.
	void turn(Report& report, std::size_t camera, double angle, const math::Vec3d& axis)
	{
		const math::RigidTransformd& t = report.cameras[camera].referenceFromCamera;
		report.cameras[camera].referenceFromCamera =
			math::RigidTransformd{math::angleAxis(angle, math::normalize(axis)) * t.rotation(), t.translation()};
	}

	std::vector<std::string> idsOf(const feature::TrackSet& set)
	{
		std::vector<std::string> out;
		for (const feature::Track& t : set.tracks)
			out.push_back(t.identity);
		return out;
	}

	HeldOutEvidence evidenceOf(const std::variant<HeldOutEvidence, Unavailable>& result)
	{
		if (const auto* unavailable = std::get_if<Unavailable>(&result))
			FAIL("no held-out evidence: " << unavailable->reason);
		return std::get<HeldOutEvidence>(result);
	}

	double perCamera(const HeldOutEvidence& e, std::size_t camera)
	{
		for (const auto& [id, angle] : e.perCamera)
		{
			if (id.value == scene::identityOf(camera))
				return angle;
		}
		return -1;
	}

	std::vector<standin::RefinerScript::Asked> asked()
	{
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		return standin::refinerScript().asked;
	}

	void requireSame(const HeldOutEvidence& a, const HeldOutEvidence& b)
	{
		CHECK(a.units == b.units);
		CHECK(a.predictions == b.predictions);
		CHECK(a.unpredicted == b.unpredicted);
		CHECK(a.residuals == b.residuals);
		CHECK(a.epipolar == b.epipolar);
		CHECK(a.rmsAngle == b.rmsAngle);
		CHECK(a.rmsPixels == b.rmsPixels);
		CHECK(a.worstPixels == b.worstPixels);
		REQUIRE(a.perCamera.size() == b.perCamera.size());
		for (std::size_t i = 0; i < a.perCamera.size(); ++i)
		{
			CHECK(a.perCamera[i].first == b.perCamera[i].first);
			CHECK(a.perCamera[i].second == b.perCamera[i].second);
		}
	}
} // namespace

TEST_CASE("held-out tracks of a true registration are predicted exactly", "[camera][registration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::trackSetOf();
	REQUIRE(set.tracks.size() > 40);
	const HeldOutEvidence e = evidenceOf(validate(rig(), truthReport(), set, idsOf(set)));
	std::size_t members = 0;
	for (const feature::Track& t : set.tracks)
		members += t.observations.size();
	CHECK(e.units == set.tracks.size());
	CHECK(e.predictions == members);
	CHECK(e.residuals == e.predictions);
	CHECK(e.unpredicted == 0);
	CHECK(e.rmsAngle < 1e-9);
	CHECK(e.worstPixels < 1e-6);
	CHECK(e.perCamera.size() == 4);
}

TEST_CASE("a camera registered 5 mrad out is seen in its own held-out members", "[camera][registration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::trackSetOf();
	Report report = truthReport();
	turn(report, 2, 0.005, math::Vec3d{0, 1, 0});
	const HeldOutEvidence e = evidenceOf(validate(rig(), report, set, idsOf(set)));
	// Each of camera 2's members is predicted from the three true cameras, so the point is exact and its
	// residual is the turn's effect on its ray: up to 5 mrad, less for a ray off perpendicular to the
	// axis. Measured 4.96 mrad. Had a member taken part in its own prediction, its wrong ray would have
	// pulled the point towards itself and hidden part of the turn.
	const double own = perCamera(e, 2);
	CHECK(own > 0.0049);
	CHECK(own <= 0.005);
	// The true cameras' members are predicted through camera 2 as well, so they carry its error too:
	// measured 1.50 and 1.65 mrad for cameras 0 and 1, and 6.16 mrad for camera 3, at the end of the arc,
	// where the point is placed mostly by camera 2, its neighbour.
	CHECK(perCamera(e, 0) > 0.001);
	CHECK(perCamera(e, 3) > own);
}

TEST_CASE("each held-out track is refined on its own, every camera held, under one fixed procedure",
		  "[camera][registration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::trackSetOf();
	Report report = truthReport();
	turn(report, 2, 0.005, math::Vec3d{0, 1, 0});
	report.reproducibility.request.loss = RobustLoss{LossFamily::Huber, 0.5};
	report.reproducibility.request.noise = NoiseModel{3.0};
	std::vector<std::string> ids;
	std::size_t transfers = 0;
	for (const feature::Track& t : set.tracks)
	{
		if (t.observations.size() >= 3)
		{
			ids.push_back(t.identity);
			++transfers;
		}
	}
	REQUIRE(transfers > 10);
	const HeldOutEvidence e = evidenceOf(validate(rig(), report, set, ids));
	CHECK(e.epipolar == 0);
	const std::vector<standin::RefinerScript::Asked> calls = asked();
	REQUIRE(calls.size() == transfers);
	for (const standin::RefinerScript::Asked& call : calls)
	{
		CHECK_FALSE(call.freeCameras);
		CHECK(call.bodies == 0);
		CHECK(call.cameras == call.landmarks);									   // only the track's own cameras
		CHECK(call.landmarkObservations == call.landmarks * (call.landmarks - 1)); // each member from the others
		CHECK(call.loss.family == LossFamily::None);
		CHECK(call.noise.pixelSigma == NoiseModel{}.pixelSigma);
	}

	// What the report asked of its own refinement changes nothing here.
	Report plain = truthReport();
	turn(plain, 2, 0.005, math::Vec3d{0, 1, 0});
	requireSame(e, evidenceOf(validate(rig(), plain, set, ids)));
}

TEST_CASE("a two-member track is scored against the epipolar plane, counted twice", "[camera][registration][targetless]")
{
	setUp(2);
	const feature::TrackSet set = scene::trackSetOf();
	REQUIRE(set.tracks.size() > 40);

	SECTION("exactly, when the registration is true")
	{
		const HeldOutEvidence e = evidenceOf(validate(rig(), truthReport(), set, idsOf(set)));
		CHECK(e.epipolar == e.predictions);
		CHECK(e.predictions == 2 * set.tracks.size());
		CHECK(e.rmsAngle < 1e-9);
		CHECK(e.rmsPixels == 0); // no predicted point, so no predicted pixel
		CHECK(asked().empty());	 // and no refinement
	}
	SECTION("a turn about the baseline tilts every epipolar plane by the turn")
	{
		Report report = truthReport();
		const math::Vec3d baseline =
			report.cameras[1].referenceFromCamera.translation() - report.cameras[0].referenceFromCamera.translation();
		turn(report, 1, 0.005, baseline);
		const HeldOutEvidence e = evidenceOf(validate(rig(), report, set, idsOf(set)));
		// A turn by t about the baseline turns each epipolar plane by t about it, and a ray at an angle f
		// to the baseline then lies asin(sin t sin f) off its old plane: each member, of either camera,
		// at its own f. One degree of freedom each, so each counts twice. Worked from the truth, not from
		// the code: measured 6.30 mrad, against 4.46 counted once.
		const math::Vec3d centre0 = scene::scene().referenceFromCamera[0].translation();
		const math::Vec3d centre1 = scene::scene().referenceFromCamera[1].translation();
		double sum = 0;
		std::size_t members = 0;
		for (const feature::Track& t : set.tracks)
		{
			const std::size_t l = std::stoul(t.identity.substr(5));
			const math::Vec3d point = scene::scene().landmarks[l].point;
			for (const math::Vec3d& centre : {centre0, centre1})
			{
				const math::Vec3d ray = point - centre, baseline = centre1 - centre0;
				const double f = std::atan2(math::length(math::cross(ray, baseline)), math::dot(ray, baseline));
				const double off = std::asin(std::sin(0.005) * std::sin(f));
				sum += 2 * off * off;
				++members;
			}
		}
		CHECK(e.epipolar == members);
		CHECK_THAT(e.rmsAngle, WithinRel(std::sqrt(sum / double(members)), 1e-9));
	}
	SECTION("a pair that meets behind a camera is unpredicted, which the plane alone could not see")
	{
		Report report = truthReport();
		turn(report, 1, 3.141592653589793, math::Vec3d{0, 1, 0});
		const auto result = validate(rig(), report, set, idsOf(set));
		REQUIRE(std::holds_alternative<Unavailable>(result));
		CHECK_THAT(std::get<Unavailable>(result).reason, ContainsSubstring("could be predicted"));
	}
	SECTION("with no baseline there is no plane")
	{
		Report report = truthReport();
		report.cameras[1].referenceFromCamera = math::RigidTransformd{
			report.cameras[1].referenceFromCamera.rotation(), report.cameras[0].referenceFromCamera.translation()};
		CHECK(std::holds_alternative<Unavailable>(validate(rig(), report, set, idsOf(set))));
	}
}

TEST_CASE("held-out evidence is unavailable when there is nothing to predict", "[camera][registration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::trackSetOf();
	SECTION("a failed registration")
	{
		Report failed;
		const auto result = validate(rig(), failed, set, idsOf(set));
		REQUIRE(std::holds_alternative<Unavailable>(result));
		CHECK_THAT(std::get<Unavailable>(result).reason, ContainsSubstring("failed"));
	}
	SECTION("no track held out")
	{
		CHECK(std::holds_alternative<Unavailable>(validate(rig(), truthReport(), set, {})));
	}
	SECTION("a track the set does not hold, which is named")
	{
		std::vector<std::string> ids = idsOf(set);
		ids.push_back("track99999");
		const auto result = validate(rig(), truthReport(), set, ids);
		REQUIRE(std::holds_alternative<Unavailable>(result));
		CHECK_THAT(std::get<Unavailable>(result).reason, ContainsSubstring("track99999"));
	}
	SECTION("a camera the report did not register predicts nothing, and is predicted from nothing")
	{
		Report report = truthReport();
		report.cameras.erase(report.cameras.begin() + 3);
		const HeldOutEvidence e = evidenceOf(validate(rig(), report, set, idsOf(set)));
		CHECK(perCamera(e, 3) < 0);
		CHECK(e.rmsAngle < 1e-9);
	}
}

TEST_CASE("held-out evidence is the same in parallel, serially, and for ids in any order", "[camera][registration][targetless]")
{
	setUp();
	const feature::TrackSet set = scene::trackSetOf(0.3, 7);
	Report report = truthReport();
	turn(report, 2, 0.005, math::Vec3d{0, 1, 0});
	std::vector<std::string> ids = idsOf(set);

	const HeldOutEvidence serial = evidenceOf(validate(rig(), report, set, ids, ExecutionPolicy::DeterministicDebug));
	const lain::testing::ThreadPool pool{4};
	requireSame(serial, evidenceOf(validate(rig(), report, set, ids, ExecutionPolicy::Normal)));
	std::reverse(ids.begin(), ids.end());
	ids.push_back(ids.front()); // a track named twice counts once
	requireSame(serial, evidenceOf(validate(rig(), report, set, ids, ExecutionPolicy::Normal)));
	CHECK(serial.rmsAngle > 0.001);
}
