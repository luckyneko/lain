// The board registration method module through its production entry points, registerCameras(), over
// stand-in backends answering from a known rig (syntheticrig.h): a pose solver that may be noisy or
// rank a plane's other pose first, and a refiner that hands back its starting estimate, so what is
// under test is everything the module decides around them. Its own executable: the registries only
// grow, and these stand-ins must be the only backends registered.

#include "syntheticrig.h"

#include <lain/camera/registration/board.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <variant>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
using namespace lain::camera::testing;
namespace method = lain::camera::registration::board;
namespace cb = lain::camera::board;

namespace
{
	// This executable tests the module around a refinement, not a refinement: the stand-in hands back
	// its starting estimate.
	const bool kPassThrough = (rig::registerPassThroughRefiner(), true);

	Report run(const Request& request = {})
	{
		return method::registerCameras(rig::cameras(), rig::groups(), specification(), request);
	}

	// A board search unlike the default in every field, so a record that fell back to the default
	// cannot pass for it.
	cb::DetectionRequest unusualSearch()
	{
		cb::DetectionRequest search;
		search.scale = cb::LongestSide{800};
		search.refineAtNativeResolution = false;
		search.detail = cb::DetailLevel::Detailed;
		search.minimumCorners = 6;
		return search;
	}

	bool isUnusualSearch(const cb::DetectionRequest& search)
	{
		const auto* longest = std::get_if<cb::LongestSide>(&search.scale);
		return longest != nullptr && longest->pixels == 800 && !search.refineAtNativeResolution &&
			   search.detail == cb::DetailLevel::Detailed && search.minimumCorners == 6;
	}

	const BoardRecord& boardRecord(const Report& report)
	{
		return std::get<BoardRecord>(report.reproducibility.method);
	}

	const BoardDiagnostics& boardDiagnostics(const Report& report)
	{
		return std::get<BoardDiagnostics>(report.diagnostics.method);
	}

	bool failedWith(const Report& report, Failure failure)
	{
		return report.status == RegistrationStatus::Failed && report.cameras.empty() && !report.failures.empty() &&
			   report.failures.front().failure == failure;
	}

	std::size_t indexOf(const capture::CameraIdentity& camera)
	{
		for (std::size_t c = 0; c < rig::scene().referenceFromCamera.size(); ++c)
		{
			if (rig::identityOf(c) == camera.value)
				return c;
		}
		FAIL("no rig camera is " << camera.value);
		return 0;
	}

	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const math::Quatd r = math::conjugate(a.rotation()) * b.rotation();
		return 2.0 * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::abs(r.w));
	}

	// Every registered camera against the truth relative to the report's reference.
	void requireTruth(const Report& report, double rotation, double translation)
	{
		REQUIRE(report.status == RegistrationStatus::Succeeded);
		REQUIRE(report.reference.has_value());
		REQUIRE(report.cameras.size() == rig::scene().referenceFromCamera.size());
		const std::size_t reference = indexOf(*report.reference);
		for (const RegisteredCamera& c : report.cameras)
		{
			CAPTURE(c.camera.value);
			const math::RigidTransformd truth = rig::trueReferenceFromCamera(reference, indexOf(c.camera));
			CHECK(rotationBetween(c.referenceFromCamera, truth) < rotation);
			CHECK(math::length(c.referenceFromCamera.translation() - truth.translation()) < translation);
		}
	}

	std::string groupIdentity(std::size_t g)
	{
		return rig::group(g).identity();
	}
} // namespace

TEST_CASE("a rig that saw the board together registers exactly, Ready", "[camera][registration]")
{
	rig::reset();
	const Report report = run();
	requireTruth(report, 1e-9, 1e-9);
	CHECK(report.verdict == Verdict::Ready);
	CHECK(report.fitnessNotes.empty());
	CHECK(report.scale.scale == Scale::Metric);
	CHECK(report.scale.evidence.find("24 mm") != std::string::npos);
	CHECK(report.thresholds.name == "registration/1");
	// Every camera shares every group, so the reference is the lowest identity.
	CHECK(report.reference->value == "cam00");
	CHECK_FALSE(report.referenceRequested);
	CHECK(report.referenceFromCamera(capture::CameraIdentity{"cam00"})->translation() == math::Vec3d{0.0});

	const Diagnostics& d = report.diagnostics;
	const BoardDiagnostics& board = boardDiagnostics(report);
	CHECK(board.groupsExamined == 24);
	CHECK(board.groupsUsable == 24);
	CHECK(board.observations == 96);
	CHECK(board.withoutPose == 0);
	CHECK(d.edges.size() == 6);
	CHECK(d.components.size() == 1);
	CHECK(d.weakBridges.empty());
	CHECK(board.flips.empty());
	CHECK(d.outliers.empty());
	CHECK(d.heldOutUnits.size() == 5); // every fifth of 24, from the third
	CHECK(d.medianDepth > 0.9);
	CHECK(d.medianDepth < 1.1);
	REQUIRE(d.refinement.has_value());
	CHECK(d.refinement->bodies == 19);
	REQUIRE(d.cameras.size() == 4);
	for (const CameraEvidence& c : d.cameras)
	{
		CHECK(c.shared == 19);
		CHECK(c.applicability == Applicability::Unknown);
		CHECK(c.outliers == 0);
		CHECK(c.rmsPixels < 1e-6);
	}
	REQUIRE(board.detections.size() == 24);
	CHECK(std::is_sorted(board.detections.begin(), board.detections.end(),
						 [](const GroupDetections& a, const GroupDetections& b)
						 { return a.group < b.group; }));

	const auto* held = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(held != nullptr);
	CHECK(held->units == 5);
	CHECK(held->predictions == 20);
	CHECK(held->unpredicted == 0);
	CHECK(held->rmsAngle < 1e-9);
	CHECK(held->perCamera.size() == 4);
	const auto* resampled = std::get_if<ResamplingEvidence>(&report.resampling);
	REQUIRE(resampled != nullptr);
	CHECK(resampled->resamples == 10);
	CHECK(resampled->rotationVariation < 1e-9);
	CHECK(resampled->translationVariation < 1e-9);

	const BoardRecord& record = std::get<BoardRecord>(report.reproducibility.method);
	CHECK(record.poseSolver.backend == "truth");
	CHECK(report.reproducibility.refiner.backend == "passthrough");
	CHECK(record.detector.backend == "truth");
	CHECK(report.reproducibility.frames == 96);
	CHECK(report.reproducibility.sources ==
		  std::vector<std::string>{"/rig/cam00", "/rig/cam01", "/rig/cam02", "/rig/cam03"});
	CHECK(report.toString().find("Ready: 4 cameras relative to cam00, metric") == 0);
}

TEST_CASE("a registration report's words follow its profile's evidence unit", "[camera][registration]")
{
	rig::reset();
	Report report = run();
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	CHECK(report.toString().find("over 5 groups, 24 of 24 groups usable") != std::string::npos);
	// The same report read as if its profile counted tracks: the held-out count is in the profile's
	// unit, while "groups usable" is board registration's own clause.
	report.thresholds.unit = EvidenceUnit::Track;
	CHECK(report.toString().find("over 5 tracks, 24 of 24 groups usable") != std::string::npos);
}

TEST_CASE("a partially overlapping rig registers through a chain of bridges", "[camera][registration]")
{
	// Each camera sees only its neighbours' part of the room: 0-1, 1-2, 2-3.
	rig::reset(4, 36);
	rig::seenBy(0, 12, {0, 1});
	rig::seenBy(12, 24, {1, 2});
	rig::seenBy(24, 36, {2, 3});
	const Report report = run();
	requireTruth(report, 1e-9, 1e-9);
	const Diagnostics& d = report.diagnostics;
	REQUIRE(d.edges.size() == 3);
	for (const GraphEdgeReport& e : d.edges)
		CHECK(e.bridge);
	CHECK(d.weakBridges.empty());
	// An end camera shares only 12 groups, and fewer once some are held out (9, as measured), which is
	// short of Ready's 10: Exploratory, and the report says which camera.
	CHECK(report.verdict == Verdict::Exploratory);
	REQUIRE(report.fitnessNotes.size() == 1);
	CHECK(report.fitnessNotes.front() == "Ready: 1 of 4 cameras in fewer than 10 shared groups (fewest: cam03, in 9)");
}

TEST_CASE("a rig hanging from one shared group names its weak bridge", "[camera][registration]")
{
	rig::reset(4, 25);
	rig::seenBy(0, 12, {0, 1});
	rig::seenBy(12, 13, {1, 2});
	rig::seenBy(13, 25, {2, 3});
	const Report report = run();
	// Connected, and with exact poses exact; but cameras 2 and 3 rest on a single view.
	requireTruth(report, 1e-9, 1e-9);
	REQUIRE(report.diagnostics.weakBridges.size() == 1);
	const GraphEdgeReport& bridge = report.diagnostics.weakBridges.front();
	CHECK(bridge.a.value == "cam01");
	CHECK(bridge.b.value == "cam02");
	CHECK(bridge.shared == 1);
	CHECK(report.verdict == Verdict::Rejected);
	CHECK(std::any_of(report.fitnessNotes.begin(), report.fitnessNotes.end(), [](const std::string& note)
					  { return note == "Exploratory: weak bridges: 1 under 2 shared groups (first: cam01-cam02, on 1)"; }));
	// The one group holding the rig together is never held out, whatever the stride lands on.
	const std::vector<std::string>& held = report.diagnostics.heldOutUnits;
	CHECK(std::find(held.begin(), held.end(), groupIdentity(12)) == held.end());
}

TEST_CASE("a capture group that alone joins two cameras is never held out", "[camera][registration]")
{
	// A chain of seven cameras, each neighbouring pair sharing exactly one group: every group holds
	// the rig together, so whichever the stride lands on must stay in.
	rig::reset(7, 6);
	for (std::size_t g = 0; g < 6; ++g)
		rig::seenBy(g, g + 1, {g, g + 1});
	const Report report = run();
	requireTruth(report, 1e-9, 1e-9);
	CHECK(report.diagnostics.heldOutUnits.empty());
	REQUIRE(std::holds_alternative<Unavailable>(report.heldOut));
	CHECK(std::get<Unavailable>(report.heldOut).reason == "no capture groups were held out");
}

TEST_CASE("a disconnected rig fails, with each component placed as a diagnostic", "[camera][registration]")
{
	rig::reset(4, 24);
	rig::seenBy(0, 12, {0, 1});
	rig::seenBy(12, 24, {3, 2});
	const Report report = run();
	REQUIRE(failedWith(report, Failure::Disconnected));
	CHECK(report.failures.front().detail.find("{cam00, cam01} {cam02, cam03}") != std::string::npos);
	const Diagnostics& d = report.diagnostics;
	REQUIRE(d.components.size() == 2);
	REQUIRE(d.componentEstimates.size() == 2);
	for (const ComponentEstimate& estimate : d.componentEstimates)
	{
		REQUIRE(estimate.cameras.size() == 2);
		const std::size_t root = indexOf(estimate.reference);
		for (const RegisteredCamera& c : estimate.cameras)
		{
			const math::RigidTransformd truth = rig::trueReferenceFromCamera(root, indexOf(c.camera));
			CHECK(rotationBetween(c.referenceFromCamera, truth) < 1e-9);
			CHECK(math::length(c.referenceFromCamera.translation() - truth.translation()) < 1e-9);
		}
	}
	CHECK(report.toString().find("Failed: Disconnected") == 0);
}

TEST_CASE("a planar board's other pose, ranked first, is resolved by the other cameras", "[camera][registration]")
{
	// Camera 3's pose solver ranks the wrong pose first in every group: no single view can tell, and
	// a registration that trusted the ranking would place camera 3 wrongly.
	rig::reset();
	for (std::size_t g = 0; g < 24; ++g)
		rig::scene().flipped.insert({3, g});
	const Report report = run();
	requireTruth(report, 1e-9, 1e-9);
	const BoardDiagnostics& board = boardDiagnostics(report);
	REQUIRE(board.flips.size() >= 15);
	for (const FlipChoice& flip : board.flips)
		CHECK(flip.camera.value == "cam03");
	CHECK(report.diagnostics.outliers.empty());
}

TEST_CASE("a capture group whose frames disagree is outvoted, and its stray named", "[camera][registration]")
{
	// In group 5 camera 2's frame shows the board 6 cm and 0.2 rad from where the others saw it: a
	// frame from another instant, grouped with the wrong ones.
	rig::reset();
	const math::RigidTransformd& actual = rig::scene().referenceFromBoard[5];
	rig::scene().elsewhere[{2, 5}] =
		math::RigidTransformd{math::angleAxis(0.2, math::Vec3d{0, 0, 1}), math::Vec3d{0.06, 0, 0}} * actual;
	Request request;
	request.heldOutFraction = 0; // so group 5 is certainly fitted
	const Report report = run(request);
	requireTruth(report, 1e-9, 1e-9);
	const Diagnostics& d = report.diagnostics;
	REQUIRE(d.outliers.size() == 1);
	CHECK(d.outliers[0].unit == groupIdentity(5));
	CHECK(d.outliers[0].camera.value == "cam02");
	// The board moved 6 cm at 1 m, some 30 px at f = 500: measured at 66 standard deviations.
	CHECK(d.outliers[0].rmsWhitened > 10 * request.loss.scale);
	CHECK(d.cameras[2].outliers == 1);
}

TEST_CASE("a requested reference is the identity, and every camera is placed relative to it", "[camera][registration]")
{
	rig::reset();
	Request request;
	request.reference = capture::CameraIdentity{"cam02"};
	const Report report = run(request);
	requireTruth(report, 1e-9, 1e-9);
	CHECK(report.reference->value == "cam02");
	CHECK(report.referenceRequested);
	const math::RigidTransformd self = *report.referenceFromCamera(capture::CameraIdentity{"cam02"});
	CHECK(self.translation() == math::Vec3d{0.0});
	CHECK(self.rotation() == math::Quatd{1, 0, 0, 0});
}

TEST_CASE("the reference is the camera in the most shared groups when none is requested", "[camera][registration]")
{
	rig::reset(4, 30);
	rig::seenBy(0, 10, {0, 1});
	rig::seenBy(10, 30, {1, 2, 3});
	Request request;
	request.heldOutFraction = 0;
	const Report report = run(request);
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	// cam01 shares all 30 groups; cam02 and cam03 share 20, cam00 10.
	CHECK(report.reference->value == "cam01");
}

TEST_CASE("held-out groups validate and never reach the refinement", "[camera][registration]")
{
	rig::reset();
	Request request;
	request.resamples = 0;
	const Report report = run(request);
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	const std::size_t held = report.diagnostics.heldOutUnits.size();
	REQUIRE(held == 5);
	std::size_t global = 0, single = 0;
	for (const rig::RefinerScript::Asked& asked : rig::refinerScript().asked)
	{
		CHECK(asked.landmarks == 0); // a board registration observes boards
		CHECK(asked.landmarkObservations == 0);
		if (asked.freeCameras)
		{
			++global;
			CHECK(asked.bodies == 24 - held);
			// Four cameras of 24 corners each per fitted group, no more.
			CHECK(asked.observations == (24 - held) * 4 * 24);
		}
		else
		{
			++single;
			CHECK(asked.bodies == 1);
			CHECK(asked.observations == 3 * 24); // the other three members
		}
	}
	CHECK(global == 1);
	CHECK(single == held * 4);
	CHECK(std::holds_alternative<Unavailable>(report.resampling));
}

TEST_CASE("a noisy rig is measured: held-out transfer and stability report the noise", "[camera][registration]")
{
	rig::reset();
	rig::scene().poseNoise = 0.002;
	const Report report = run();
	requireTruth(report, 0.01, 0.01);
	const auto* held = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(held != nullptr);
	// Measured: 0.027 mrad held out, against a rotation spread of 0.69 mrad and a translation spread of
	// 0.05% of the depth. The noise turns each board about its centre, which moves its corners little
	// (a transfer barely sees it) but turns the relative pose a group proposes by as much (resamples
	// do): the stand-in refiner keeps whichever proposal initialisation chose.
	CHECK(held->rmsAngle > 1e-6);
	CHECK(held->rmsAngle < 0.001);
	const auto* resampled = std::get_if<ResamplingEvidence>(&report.resampling);
	REQUIRE(resampled != nullptr);
	CHECK(resampled->rotationVariation > 1e-5);
	CHECK(resampled->rotationVariation < 0.01);
	CHECK(resampled->translationVariation > 1e-5);
	CHECK(resampled->translationVariation < 0.01);
}

TEST_CASE("the same work in parallel, serially, or listed in another order gives the same report", "[camera][registration]")
{
	rig::reset();
	rig::scene().poseNoise = 0.002;
	rig::scene().flipped.insert({1, 4});
	const lain::testing::ThreadPool pool;
	Request parallel;
	Request serial;
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const Report a = run(parallel);
	const Report b = run(serial);

	std::vector<method::RigCamera> cameras = rig::cameras();
	std::vector<method::GroupObservations> groups = rig::groups();
	std::reverse(cameras.begin(), cameras.end());
	std::reverse(groups.begin(), groups.end());
	const Report c = method::registerCameras(cameras, groups, specification(), parallel);

	for (const Report* other : {&b, &c})
	{
		REQUIRE(other->status == RegistrationStatus::Succeeded);
		CHECK(other->toString() == a.toString());
		REQUIRE(other->cameras.size() == a.cameras.size());
		for (std::size_t i = 0; i < a.cameras.size(); ++i)
		{
			CHECK(other->cameras[i].camera == a.cameras[i].camera);
			CHECK(other->cameras[i].referenceFromCamera.rotation() == a.cameras[i].referenceFromCamera.rotation());
			CHECK(other->cameras[i].referenceFromCamera.translation() == a.cameras[i].referenceFromCamera.translation());
		}
		CHECK(other->diagnostics.heldOutUnits == a.diagnostics.heldOutUnits);
		CHECK(std::get<HeldOutEvidence>(other->heldOut).rmsAngle == std::get<HeldOutEvidence>(a.heldOut).rmsAngle);
		const auto& ra = std::get<ResamplingEvidence>(a.resampling);
		const auto& rb = std::get<ResamplingEvidence>(other->resampling);
		CHECK(rb.rotationVariation == ra.rotationVariation);
		CHECK(rb.translationVariation == ra.translationVariation);
		CHECK(rb.worstCamera == ra.worstCamera);
		CHECK(boardDiagnostics(*other).flips.size() == boardDiagnostics(a).flips.size());
		CHECK(other->verdict == a.verdict);
	}
}

TEST_CASE("footage is detected member by member, then registered", "[camera][registration]")
{
	rig::reset(3, 20);
	const std::vector<method::RigFootage> footage = rig::footage();
	std::vector<capture::CameraFootage> byPosition;
	for (const method::RigFootage& c : footage)
		byPosition.push_back({c.camera, c.footage});
	const capture::GroupingResult grouping = capture::groupsByPosition(byPosition);
	REQUIRE(grouping.groups.size() == 20);
	const Report report = method::registerCameras(footage, grouping.groups, specification(), unusualSearch(), Request{});
	requireTruth(report, 1e-9, 1e-9);
	CHECK(boardDiagnostics(report).observations == 60);

	// The search reaches every frame's detection, and the record says what it was.
	for (const GroupDetections& g : boardDiagnostics(report).detections)
	{
		for (const auto& member : g.members)
			CHECK(isUnusualSearch(member.second.request));
	}
	REQUIRE(boardRecord(report).detection.has_value());
	CHECK(isUnusualSearch(*boardRecord(report).detection));

	SECTION("a member whose frame its footage lacks is refused before anything is decoded")
	{
		std::vector<capture::CaptureGroup> groups = grouping.groups;
		media::FrameRef missing = rig::frameOf(0, 999);
		groups.push_back(*capture::CaptureGroup::create({{capture::CameraIdentity{"cam00"}, missing}}).group);
		const Report refused = method::registerCameras(footage, groups, specification(), unusualSearch(), Request{});
		REQUIRE(failedWith(refused, Failure::InvalidDataset));
		CHECK(refused.failures.front().detail.find("frame 999 of /rig/cam00") != std::string::npos);
		CHECK(boardDiagnostics(refused).detections.empty());
		// Nothing was searched, and the record still says what would have been.
		REQUIRE(boardRecord(refused).detection.has_value());
		CHECK(isUnusualSearch(*boardRecord(refused).detection));
	}
}

TEST_CASE("a registration over given detections records the search they report", "[camera][registration]")
{
	rig::reset();
	std::vector<method::GroupObservations> groups = rig::groups();
	for (method::GroupObservations& g : groups)
	{
		for (cb::DetectionReport& d : g.detections)
			d.request = unusualSearch();
	}
	// The registration's own request holds no search at all: what the detections were made with is
	// theirs to say.
	const Report report = method::registerCameras(rig::cameras(), groups, specification(), Request{});
	REQUIRE(report.status == RegistrationStatus::Succeeded);
	REQUIRE(boardRecord(report).detection.has_value());
	CHECK(isUnusualSearch(*boardRecord(report).detection));
	CHECK(boardRecord(report).detector.backend == "truth");

	SECTION("no detection given, no search recorded")
	{
		const Report empty = method::registerCameras(rig::cameras(), {}, specification(), Request{});
		CHECK(empty.status == RegistrationStatus::Failed);
		CHECK_FALSE(boardRecord(empty).detection.has_value());
		CHECK(boardRecord(empty).detector.backend.empty());
	}
}

TEST_CASE("a dataset or request that cannot register is refused before any work", "[camera][registration]")
{
	rig::reset();
	SECTION("an unknown fitness profile")
	{
		Request request;
		request.fitnessProfile = "registration/0";
		CHECK(failedWith(run(request), Failure::UnknownFitnessProfile));
	}
	SECTION("a profile that counts feature tracks, which a board registration has none of")
	{
		Request request;
		request.fitnessProfile = "registration-targetless/1";
		const Report report = run(request);
		REQUIRE(failedWith(report, Failure::IncompatibleFitnessProfile));
		CHECK(report.failures.front().detail.find("counts tracks") != std::string::npos);
	}
	SECTION("the empty name is the method's own profile, and so is registration/1 named")
	{
		CHECK(Request{}.fitnessProfile.empty());
		Request named;
		named.fitnessProfile = "registration/1";
		const Report byDefault = run();
		const Report byName = run(named);
		CHECK(byDefault.thresholds.name == "registration/1");
		CHECK(byName.thresholds.name == "registration/1");
		CHECK(byDefault.verdict == byName.verdict);
	}
	SECTION("an unknown reference")
	{
		Request request;
		request.reference = capture::CameraIdentity{"cam99"};
		CHECK(failedWith(run(request), Failure::UnknownReference));
	}
	SECTION("one camera")
	{
		rig::reset(1, 10);
		CHECK(failedWith(run(), Failure::TooFewCameras));
	}
	SECTION("two cameras with one identity")
	{
		std::vector<method::RigCamera> cameras = rig::cameras();
		cameras[1].camera = cameras[0].camera;
		CHECK(failedWith(method::registerCameras(cameras, rig::groups(), specification(), Request{}), Failure::InvalidDataset));
	}
	SECTION("a group naming a camera the dataset lacks")
	{
		std::vector<method::RigCamera> cameras = rig::cameras();
		cameras.pop_back();
		const Report report = method::registerCameras(cameras, rig::groups(), specification(), Request{});
		REQUIRE(failedWith(report, Failure::InvalidDataset));
		CHECK(report.failures.front().detail.find("names camera \"cam03\"") != std::string::npos);
	}
	SECTION("a group whose detections do not match its members")
	{
		std::vector<method::GroupObservations> groups = rig::groups();
		groups[3].detections.pop_back();
		CHECK(failedWith(method::registerCameras(rig::cameras(), groups, specification(), Request{}), Failure::InvalidDataset));
	}
	SECTION("a model of another image size")
	{
		rig::scene().modelWidth = 800;
		CHECK(failedWith(run(), Failure::IncompatibleModel));
	}
	SECTION("a model of unknown applicability, which the request refuses")
	{
		Request request;
		request.unknownApplicability = ApplicabilityPolicy::Refuse;
		CHECK(failedWith(run(request), Failure::UnknownApplicability));
	}
	SECTION("no capture group seen by two cameras")
	{
		rig::seenBy(0, 24, {0});
		CHECK(failedWith(run(), Failure::TooFewCameras));
	}
}
