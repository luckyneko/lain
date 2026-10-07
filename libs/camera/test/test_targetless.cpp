// Targetless registration (registration::targetless) and its evidence, over stand-ins answering from
// a known scene (syntheticscene.h) and a refiner that hands back its start (passthroughrefiner.h):
// held-out tracks predicted member by member (registration::validate), which cameras can be placed,
// the seed, cheirality, stability, and the refusals. Every measured number is written beside its check.

#include "passthroughrefiner.h"
#include "syntheticscene.h"

#include <lain/camera/registration/targetless.h>
#include <lain/camera/registration/validation.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
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

// --- registration::targetless ------------------------------------------------------------

namespace
{
	Report run(const feature::TrackSet& set, const Request& request = {})
	{
		return registration::targetless::registerCameras(rig(), set, request);
	}

	bool failedWith(const Report& report, Failure failure)
	{
		return report.status == RegistrationStatus::Failed && !report.failures.empty() &&
			   report.failures.front().failure == failure;
	}

	const TargetlessDiagnostics& targetlessOf(const Report& report)
	{
		return std::get<TargetlessDiagnostics>(report.diagnostics.method);
	}

	std::vector<std::string> names(const std::vector<capture::CameraIdentity>& ids)
	{
		std::vector<std::string> out;
		for (const capture::CameraIdentity& id : ids)
			out.push_back(id.value);
		return out;
	}

	// Each camera's registered transform against the truth relative to `reference`, with the truth's
	// translations scaled as the registration normalises them: by the median depth of the landmarks the
	// reference sees.
	void requireTruth(const Report& report, std::size_t reference, double tolerance = 1e-9)
	{
		REQUIRE(report.status == RegistrationStatus::Succeeded);
		const std::vector<math::RigidTransformd>& truth = scene::scene().referenceFromCamera;
		std::vector<double> depths;
		for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
		{
			if (scene::pixelOf(reference, l, 0))
				depths.push_back(truth[reference].inverse().apply(scene::scene().landmarks[l].point).z);
		}
		std::sort(depths.begin(), depths.end());
		const std::size_t half = depths.size() / 2;
		// The registration's median is over its fitting landmarks, which the hold-out thins: compare the
		// scale it chose against the truth's own instead of assuming the two medians are one.
		const math::RigidTransformd firstOther =
			truth[reference].inverse() * truth[reference == 0 ? 1 : 0];
		const double scale = math::length(report.cameras[reference == 0 ? 1 : 0].referenceFromCamera.translation()) /
							 math::length(firstOther.translation());
		CHECK(scale > 0.5 / depths[half]);
		CHECK(scale < 2.0 / depths[half]);
		for (std::size_t c = 0; c < truth.size(); ++c)
		{
			const math::RigidTransformd expected = truth[reference].inverse() * truth[c];
			const math::RigidTransformd& actual = report.cameras[c].referenceFromCamera;
			INFO("camera " << c);
			CHECK(std::abs(math::dot(expected.rotation(), actual.rotation())) > 1 - tolerance);
			CHECK(math::length(expected.translation() * scale - actual.translation()) < tolerance);
		}
	}

	// Every landmark seen only by `cameras`.
	void seenOnlyBy(std::size_t from, std::size_t to, const std::set<std::size_t>& cameras)
	{
		for (std::size_t l = from; l < to; ++l)
			scene::scene().landmarks[l].cameras = cameras;
	}

	void requireSameReport(const Report& a, const Report& b)
	{
		REQUIRE(a.status == b.status);
		REQUIRE(a.cameras.size() == b.cameras.size());
		for (std::size_t c = 0; c < a.cameras.size(); ++c)
		{
			CHECK(a.cameras[c].camera == b.cameras[c].camera);
			CHECK(a.cameras[c].referenceFromCamera.rotation() == b.cameras[c].referenceFromCamera.rotation());
			CHECK(a.cameras[c].referenceFromCamera.translation() == b.cameras[c].referenceFromCamera.translation());
		}
		CHECK(a.diagnostics.heldOutUnits == b.diagnostics.heldOutUnits);
		CHECK(a.verdict == b.verdict);
		REQUIRE(a.heldOut.index() == b.heldOut.index());
		if (const auto* e = std::get_if<HeldOutEvidence>(&a.heldOut))
			requireSame(*e, std::get<HeldOutEvidence>(b.heldOut));
		REQUIRE(a.resampling.index() == b.resampling.index());
		if (const auto* e = std::get_if<ResamplingEvidence>(&a.resampling))
		{
			const ResamplingEvidence& f = std::get<ResamplingEvidence>(b.resampling);
			CHECK(e->resamples == f.resamples);
			CHECK(e->rotationVariation == f.rotationVariation);
			CHECK(e->translationVariation == f.translationVariation);
			CHECK(e->worstCamera == f.worstCamera);
		}
		CHECK(a.diagnostics.outliers.size() == b.diagnostics.outliers.size());
	}
} // namespace

TEST_CASE("a rig that shares a static scene registers exactly, up to its scale, Ready", "[camera][registration][targetless]")
{
	setUp(4, 300);
	const feature::TrackSet set = scene::trackSetOf();
	const Report report = run(set);
	INFO(report.toString());
	requireTruth(report, 0);
	CHECK(report.reference->value == "cam00");
	CHECK_FALSE(report.referenceRequested);
	CHECK(report.thresholds.name == "registration-targetless/1");
	CHECK(report.scale.scale == Scale::Arbitrary);
	CHECK_THAT(report.scale.evidence, ContainsSubstring("median depth"));
	CHECK(report.verdict == Verdict::Ready);
	CHECK(report.diagnostics.outliers.empty());
	CHECK_THAT(report.toString(), ContainsSubstring("landmarks from"));

	const TargetlessDiagnostics& t = targetlessOf(report);
	CHECK(t.tracks == set.tracks.size());
	CHECK(t.tracksUsable == set.tracks.size());
	CHECK(t.landmarks + report.diagnostics.heldOutUnits.size() == set.tracks.size());
	CHECK(t.untriangulated == 0);
	CHECK(t.behind == 0);
	CHECK(t.seed.size() == 2);
	CHECK_FALSE(t.seedBelowFloor);
	CHECK(t.seedAngle >= 0.0349);
	CHECK(t.placement.size() == 4);
	CHECK(t.unplaceable.empty());

	// Exact tracks predict exactly, and resample to the same rig: once each resample's scale is aligned.
	const HeldOutEvidence& held = std::get<HeldOutEvidence>(report.heldOut);
	CHECK(held.units == report.diagnostics.heldOutUnits.size());
	CHECK(held.rmsAngle < 1e-9);
	const ResamplingEvidence& resampling = std::get<ResamplingEvidence>(report.resampling);
	CHECK(resampling.resamples == Request{}.resamples);
	CHECK(resampling.rotationVariation < 1e-9);
	CHECK(resampling.translationVariation < 1e-9);
}

TEST_CASE("held-out tracks never reach the refinement", "[camera][registration][targetless]")
{
	setUp(4, 300);
	const feature::TrackSet set = scene::trackSetOf();
	Request request;
	request.resamples = 0;
	const Report report = run(set, request);
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	const std::size_t held = report.diagnostics.heldOutUnits.size();
	CHECK(held == 60); // every fifth of 300
	const std::vector<standin::RefinerScript::Asked> calls = asked();
	REQUIRE_FALSE(calls.empty());
	CHECK(calls[0].freeCameras);
	CHECK(calls[0].landmarks == set.tracks.size() - held);
	CHECK(calls[0].landmarkObservations == 4 * (set.tracks.size() - held));
	// After the refinement, only held-camera problems: one per held-out track.
	CHECK(calls.size() == 1 + held);
	for (std::size_t i = 1; i < calls.size(); ++i)
		CHECK_FALSE(calls[i].freeCameras);
}

TEST_CASE("a partially overlapping rig registers through the landmarks its cameras share", "[camera][registration][targetless]")
{
	setUp(6, 400);
	// Each landmark is seen by three neighbouring cameras, so every camera beyond the seed shares
	// landmarks with two already placed.
	for (std::size_t l = 0; l < 400; ++l)
		scene::scene().landmarks[l].cameras = { l % 4,
												l % 4 + 1,
												l % 4 + 2 };
	const Report report = run(scene::trackSetOf());
	INFO(report.toString());
	requireTruth(report, report.reference->value == "cam00" ? 0 : std::stoul(report.reference->value.substr(3)));
	CHECK(targetlessOf(report).placement.size() == 6);
}

TEST_CASE("a chain of camera pairs fails, naming the cameras it cannot place", "[camera][registration][targetless]")
{
	setUp(4, 150);
	// Pairs (0, 1), (1, 2), (2, 3), 50 landmarks each and none seen by three: connected, but each overlap
	// keeps a scale of its own, so no camera beyond the seed's pair can be placed.
	seenOnlyBy(0, 50, {0, 1});
	seenOnlyBy(50, 100, {1, 2});
	seenOnlyBy(100, 150, {2, 3});
	const Report report = run(scene::trackSetOf());
	REQUIRE(failedWith(report, Failure::Disconnected));
	CHECK(report.diagnostics.components.size() == 1); // the graph is connected
	CHECK(names(targetlessOf(report).unplaceable) == std::vector<std::string>{"cam02", "cam03"});
	CHECK_THAT(report.failures.front().detail, ContainsSubstring("cam02, cam03"));
	REQUIRE(report.diagnostics.componentEstimates.size() == 1);
	CHECK(report.diagnostics.componentEstimates[0].cameras.size() == 2);
}

TEST_CASE("a disconnected rig fails, with what each part places as a diagnostic", "[camera][registration][targetless]")
{
	setUp(4, 200);
	seenOnlyBy(0, 100, {0, 1});
	seenOnlyBy(100, 200, {2, 3});
	const Report report = run(scene::trackSetOf());
	REQUIRE(failedWith(report, Failure::Disconnected));
	CHECK(report.diagnostics.components.size() == 2);
	REQUIRE(report.diagnostics.componentEstimates.size() == 2);
	for (const ComponentEstimate& e : report.diagnostics.componentEstimates)
		CHECK(e.cameras.size() == 2);
}

TEST_CASE("a two-camera rig registers, and on few tracks its one edge is a weak bridge", "[camera][registration][targetless]")
{
	// Placing a camera needs landmarks it shares with two placed cameras, so it always joins by two
	// edges: in a placeable rig the only bridge there can be is a two-camera rig's own edge.
	SECTION("on 300 landmarks, Ready")
	{
		setUp(2, 300);
		const Report report = run(scene::trackSetOf());
		requireTruth(report, 0);
		CHECK(report.verdict == Verdict::Ready);
		const HeldOutEvidence& held = std::get<HeldOutEvidence>(report.heldOut);
		CHECK(held.epipolar == held.predictions); // every held-out track has two members
	}
	SECTION("on 50 landmarks, the edge is a weak bridge and the rig is Exploratory")
	{
		setUp(2, 50);
		const Report report = run(scene::trackSetOf());
		REQUIRE(report.status == RegistrationStatus::Succeeded);
		REQUIRE(report.diagnostics.weakBridges.size() == 1);
		CHECK(report.diagnostics.weakBridges[0].shared == 40); // 50, less the 10 held out
		CHECK(report.verdict == Verdict::Exploratory);
	}
}

TEST_CASE("a track the placement cannot spare is never held out", "[camera][registration][targetless]")
{
	// The design review's case. Cameras 0 and 1 share 20 tracks and nothing with three cameras, so their
	// seed places nobody else. Cameras 2 and 3 share 10, and 4 more each with camera 0 and with camera 1:
	// that pair places everyone, each of 0 and 1 by exactly the 4 tracks it needs. Those 8 sit where the
	// hold-out stride lands, so only the guard keeps them, anchored on the seed that places every camera.
	setUp(4, 38);
	const std::vector<std::size_t> withZero{2, 7, 12, 17}, withOne{22, 27, 32, 37};
	for (std::size_t l = 0; l < 38; ++l)
		scene::scene().landmarks[l].cameras = { 0,
												1 };
	std::size_t xy = 0;
	for (std::size_t l = 0; l < 38 && xy < 10; ++l)
	{
		if (std::count(withZero.begin(), withZero.end(), l) == 0 && std::count(withOne.begin(), withOne.end(), l) == 0)
		{
			scene::scene().landmarks[l].cameras = { 2,
													3 };
			++xy;
		}
	}
	for (const std::size_t l : withZero)
		scene::scene().landmarks[l].cameras = { 0,
												2,
												3 };
	for (const std::size_t l : withOne)
		scene::scene().landmarks[l].cameras = { 1,
												2,
												3 };
	const feature::TrackSet set = scene::trackSetOf();
	REQUIRE(set.tracks.size() == 38);
	// The precondition: every fifth track from the third is a critical one (stride 5, from 2).
	for (std::size_t u = 2; u < 38; u += 5)
		REQUIRE(set.tracks[u].identity == "track000" + std::string(u < 10 ? "0" : "") + std::to_string(u));

	const Report report = run(set);
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	for (const std::string& id : report.diagnostics.heldOutUnits)
	{
		const std::size_t l = std::stoul(id.substr(5));
		CHECK(std::count(withZero.begin(), withZero.end(), l) == 0);
		CHECK(std::count(withOne.begin(), withOne.end(), l) == 0);
	}
	CHECK(names(targetlessOf(report).seed) == std::vector<std::string>{"cam02", "cam03"});
}

TEST_CASE("a landmark behind a camera is left out of the refinement, and named", "[camera][registration][targetless]")
{
	setUp(4, 200);
	// Camera 3 stands inside the scene facing away from the others, so it sees only the landmarks
	// beyond it; a wrong match gives it one behind it.
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
	std::size_t observations = 0;
	for (const feature::Track& t : set.tracks)
		observations += t.observations.size();

	const Report report = run(set);
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	CHECK(targetlessOf(report).behind == 1);
	bool named = false;
	for (const Outlier& o : report.diagnostics.outliers)
		named = named || (o.unit == wrong->identity && o.camera.value == "cam03" && std::isinf(o.rmsWhitened));
	CHECK(named);
	// The refinement never sees it.
	const std::vector<standin::RefinerScript::Asked> calls = asked();
	REQUIRE_FALSE(calls.empty());
	std::size_t heldObservations = 0;
	for (const feature::Track& t : set.tracks)
	{
		if (std::count(report.diagnostics.heldOutUnits.begin(), report.diagnostics.heldOutUnits.end(), t.identity) > 0)
			heldObservations += t.observations.size();
	}
	CHECK(calls[0].landmarkObservations == observations - heldObservations - 1);
}

TEST_CASE("a narrow rig seeds from its widest pair, and says the floor was not cleared", "[camera][registration][targetless]")
{
	setUp(3, 200);
	// Three cameras 3 cm apart, 3 m from the scene: no pair sees it with 2 degrees of parallax.
	for (std::size_t c = 0; c < 3; ++c)
		scene::scene().referenceFromCamera[c] =
			scene::lookingAt(math::Vec3d{0.03 * double(c), -0.2, -3.0}, math::Vec3d{0.0, 0.0, 0.0});
	scene::scene().renderedFrom = scene::scene().referenceFromCamera;
	const Report report = run(scene::trackSetOf());
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	const TargetlessDiagnostics& t = targetlessOf(report);
	CHECK(t.seedBelowFloor);
	CHECK(t.seedAngle < 0.0349);
	CHECK(names(t.seed) == std::vector<std::string>{"cam00", "cam02"}); // the widest
}

TEST_CASE("the seed is the pair with the most inliers that clears the parallax floor", "[camera][registration][targetless]")
{
	setUp(3, 300);
	// Cameras 0 and 1 are 3 cm apart and share all 300 landmarks; camera 2 is far off and sees 150 of
	// them. The narrow pair has the most inliers, and the wide pairs clear the floor.
	scene::scene().referenceFromCamera[1] = scene::lookingAt(
		scene::scene().referenceFromCamera[0].translation() + math::Vec3d{0.03, 0.0, 0.0}, math::Vec3d{0.0, 0.0, 0.0});
	scene::scene().renderedFrom = scene::scene().referenceFromCamera;
	seenOnlyBy(0, 150, {0, 1, 2});
	seenOnlyBy(150, 300, {0, 1});
	const Report report = run(scene::trackSetOf());
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	const TargetlessDiagnostics& t = targetlessOf(report);
	CHECK_FALSE(t.seedBelowFloor);
	CHECK(names(t.seed) == std::vector<std::string>{"cam00", "cam02"});
}

TEST_CASE("a requested reference is the identity, and every camera is placed relative to it", "[camera][registration][targetless]")
{
	setUp(4, 300);
	Request request;
	request.reference = capture::CameraIdentity{"cam02"};
	const Report report = run(scene::trackSetOf(), request);
	CHECK(report.referenceRequested);
	CHECK(report.reference->value == "cam02");
	requireTruth(report, 2);
	const math::RigidTransformd& own = report.cameras[2].referenceFromCamera;
	CHECK(own.rotation() == math::RigidTransformd{}.rotation());
	CHECK(own.translation() == math::RigidTransformd{}.translation());
}

TEST_CASE("targetless registration is the same in parallel, serially, and listed in another order",
		  "[camera][registration][targetless]")
{
	setUp(4, 300);
	const feature::TrackSet set = scene::trackSetOf(0.3, 5);
	Request serial;
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const Report a = run(set, serial);
	REQUIRE(a.status == RegistrationStatus::Succeeded);
	REQUIRE(std::holds_alternative<ResamplingEvidence>(a.resampling));
	CHECK(std::get<ResamplingEvidence>(a.resampling).rotationVariation > 0); // noise, so resamples differ

	const lain::testing::ThreadPool pool{4};
	requireSameReport(a, run(set));
	feature::TrackSet reversed = set;
	std::reverse(reversed.tracks.begin(), reversed.tracks.end());
	std::vector<RigCamera> cameras = rig();
	std::reverse(cameras.begin(), cameras.end());
	requireSameReport(a, registration::targetless::registerCameras(cameras, reversed, Request{}));
}

TEST_CASE("registering footage is extracting its tracks, then registering them", "[camera][registration][targetless]")
{
	setUp(4, 300);
	for (std::size_t l = 0; l < 300; l += 10)
		scene::scene().landmarks[l].dynamic = true; // moving content, dropped by extraction
	const std::vector<RigFootage> footage = scene::footage();
	const std::vector<capture::CaptureGroup> groups = scene::groups(footage);
	const feature::ExtractionRequest extraction;

	const Report report = registration::targetless::registerCameras(footage, groups, extraction, Request{});
	INFO(report.toString());
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	requireTruth(report, 0);
	const feature::ExtractionResult extracted = feature::extractTracks(footage, groups, extraction);
	REQUIRE(extracted.ok());
	requireSameReport(report, run(extracted.trackSet));

	const TargetlessDiagnostics& t = targetlessOf(report);
	REQUIRE(t.extraction.has_value());
	REQUIRE(t.trackSet.has_value());
	CHECK(t.trackSet->tracks.size() == extracted.trackSet.tracks.size());
	CHECK(t.tracks == 270); // the 30 moving landmarks are gone
	const TargetlessRecord& record = std::get<TargetlessRecord>(report.reproducibility.method);
	REQUIRE(record.extraction.has_value());
	CHECK(record.extraction->execution == Request{}.execution);
	CHECK(record.extractor.backend == "truth");
	CHECK(record.matcher.backend == "exact");
	CHECK(record.geometry.backend == "truth");
	CHECK(report.reproducibility.sources.size() == 4);
}

TEST_CASE("a dataset or request that cannot register is refused before any work", "[camera][registration][targetless]")
{
	setUp(4, 100);
	const feature::TrackSet set = scene::trackSetOf();
	SECTION("a profile that counts capture groups")
	{
		Request request;
		request.fitnessProfile = "registration/1";
		CHECK(failedWith(run(set, request), Failure::IncompatibleFitnessProfile));
	}
	SECTION("an unknown profile")
	{
		Request request;
		request.fitnessProfile = "registration-targetless/0";
		CHECK(failedWith(run(set, request), Failure::UnknownFitnessProfile));
	}
	SECTION("an unknown reference")
	{
		Request request;
		request.reference = capture::CameraIdentity{"cam99"};
		CHECK(failedWith(run(set, request), Failure::UnknownReference));
	}
	SECTION("two cameras with one identity")
	{
		std::vector<RigCamera> cameras = rig();
		cameras[1].camera = cameras[0].camera;
		CHECK(failedWith(registration::targetless::registerCameras(cameras, set, Request{}), Failure::InvalidDataset));
	}
	SECTION("one camera")
	{
		std::vector<RigCamera> cameras = rig();
		cameras.erase(cameras.begin() + 1, cameras.end());
		CHECK(failedWith(registration::targetless::registerCameras(cameras, set, Request{}), Failure::TooFewCameras));
	}
	SECTION("a view of a camera the dataset lacks")
	{
		std::vector<RigCamera> cameras = rig();
		cameras.pop_back();
		const Report report = registration::targetless::registerCameras(cameras, set, Request{});
		REQUIRE(failedWith(report, Failure::InvalidDataset));
		CHECK_THAT(report.failures.front().detail, ContainsSubstring("cam03"));
	}
	SECTION("two views of one camera")
	{
		feature::TrackSet bad = set;
		bad.views[1].camera = bad.views[0].camera;
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("an observation of a view the set does not have")
	{
		feature::TrackSet bad = set;
		bad.tracks[3].observations.back().view = 9;
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("two observations of one view in a track")
	{
		feature::TrackSet bad = set;
		bad.tracks[3].observations[1].view = bad.tracks[3].observations[0].view;
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("a track given twice")
	{
		feature::TrackSet bad = set;
		bad.tracks[4].identity = bad.tracks[3].identity;
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("a covariance that is not positive definite")
	{
		feature::TrackSet bad = set;
		bad.tracks[3].observations[0].covariance = std::array<double, 3>{1.0, 2.0, 1.0};
		CHECK(failedWith(run(bad), Failure::InvalidDataset));
	}
	SECTION("a model of another image size")
	{
		feature::TrackSet bad = set;
		bad.views[2].image = ImageGeometry{800, 600};
		CHECK(failedWith(run(bad), Failure::IncompatibleModel));
	}
	SECTION("footage whose capture group names a camera it lacks")
	{
		std::vector<RigFootage> footage = scene::footage();
		const std::vector<capture::CaptureGroup> groups = scene::groups(footage);
		footage.pop_back();
		CHECK(failedWith(registration::targetless::registerCameras(footage, groups, {}, Request{}), Failure::InvalidDataset));
	}
	SECTION("an extraction the request's own policy refuses")
	{
		const std::vector<RigFootage> footage = scene::footage();
		feature::ExtractionRequest extraction;
		extraction.matching.search = feature::MatchSearch::Approximate;
		Request request;
		request.execution = ExecutionPolicy::DeterministicDebug;
		const Report report =
			registration::targetless::registerCameras(footage, scene::groups(footage), extraction, request);
		REQUIRE(failedWith(report, Failure::ExtractionFailed));
		CHECK_THAT(report.failures.front().detail, ContainsSubstring("approximate"));
	}
}
