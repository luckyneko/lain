// Feature-track extraction (feature::extractTracks) over stand-ins answering from a known scene: which
// features are static, which matches join, which tracks are accepted, and that none of it depends on
// the input's order or on the execution policy. Every count is the scene's own, worked out beside its
// check.

#include "syntheticscene.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
namespace scene = lain::camera::testing::scene;
using Catch::Matchers::ContainsSubstring;

namespace
{
	// The scene's own answer: which cameras see landmark `l` in at least half of the sampled frames.
	std::vector<std::size_t> staticIn(std::size_t l, const std::vector<std::size_t>& ordinals)
	{
		std::vector<std::size_t> out;
		for (std::size_t c = 0; c < scene::scene().referenceFromCamera.size(); ++c)
		{
			std::size_t seen = 0;
			for (const std::size_t k : ordinals)
				seen += scene::pixelOf(c, l, k) ? 1 : 0;
			if (seen * 2 >= ordinals.size() && seen > 0)
				out.push_back(c);
		}
		return out;
	}

	// The track holding `pixel` in view `view`, or nullptr.
	const Track* trackAt(const TrackSet& set, std::uint32_t view, const math::Vec2d& pixel)
	{
		for (const Track& t : set.tracks)
		{
			for (const SceneObservation& o : t.observations)
			{
				if (o.view == view && math::length(o.pixel - pixel) < 1e-9)
					return &t;
			}
		}
		return nullptr;
	}

	// Whether any track holds landmark `l` as it was at any frame, in any view.
	bool anyTrackHolds(const TrackSet& set, std::size_t l)
	{
		for (std::uint32_t v = 0; v < set.views.size(); ++v)
		{
			for (std::size_t k = 0; k < scene::scene().frames; ++k)
			{
				if (const std::optional<math::Vec2d> pixel = scene::pixelOf(v, l, k); pixel && trackAt(set, v, *pixel))
					return true;
			}
		}
		return false;
	}

	std::vector<std::size_t> ordinalsOf(const TrackSet& set)
	{
		std::vector<std::size_t> out;
		for (const capture::CaptureGroup& g : set.groups)
			out.push_back(g.members().front().frame.ordinal);
		return out;
	}

	ExtractionResult run(const ExtractionRequest& request = {})
	{
		const std::vector<RigFootage> cameras = scene::footage();
		return extractTracks(cameras, scene::groups(cameras), request);
	}

	void requireEqual(const TrackSet& a, const TrackSet& b)
	{
		REQUIRE(a.groups.size() == b.groups.size());
		for (std::size_t g = 0; g < a.groups.size(); ++g)
			CHECK(a.groups[g].identity() == b.groups[g].identity());
		REQUIRE(a.views.size() == b.views.size());
		for (std::size_t v = 0; v < a.views.size(); ++v)
		{
			CHECK(a.views[v].camera == b.views[v].camera);
			CHECK(a.views[v].frames == b.views[v].frames);
		}
		REQUIRE(a.tracks.size() == b.tracks.size());
		for (std::size_t t = 0; t < a.tracks.size(); ++t)
		{
			CHECK(a.tracks[t].identity == b.tracks[t].identity);
			REQUIRE(a.tracks[t].observations.size() == b.tracks[t].observations.size());
			for (std::size_t o = 0; o < a.tracks[t].observations.size(); ++o)
			{
				const SceneObservation& x = a.tracks[t].observations[o];
				const SceneObservation& y = b.tracks[t].observations[o];
				CHECK(x.view == y.view);
				CHECK(x.pixel == y.pixel);
				CHECK(x.support == y.support);
				CHECK(x.covariance == y.covariance);
			}
		}
	}
} // namespace

TEST_CASE("a static scene gives one track per shared landmark, exactly", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	const ExtractionResult result = run();
	REQUIRE(result.ok());
	const TrackSet& set = result.trackSet;

	// Five of nine groups, at the centres of five equal bins.
	CHECK(ordinalsOf(set) == std::vector<std::size_t>{0, 2, 4, 6, 8});
	REQUIRE(set.views.size() == 4);
	for (std::uint32_t v = 0; v < 4; ++v)
	{
		CHECK(set.views[v].camera.value == scene::identityOf(v));
		CHECK(set.views[v].image.width == 640);
		REQUIRE(set.views[v].frames.size() == 5);
		CHECK(set.views[v].frames[2].ordinal == 4);
	}

	std::size_t expected = 0;
	for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
	{
		const std::vector<std::size_t> cameras = staticIn(l, {0, 2, 4, 6, 8});
		if (cameras.size() < 2)
			continue;
		++expected;
		const Track* track = trackAt(set, std::uint32_t(cameras.front()), *scene::pixelOf(cameras.front(), l, 0));
		REQUIRE(track != nullptr);
		REQUIRE(track->observations.size() == cameras.size());
		for (std::size_t o = 0; o < cameras.size(); ++o)
		{
			const SceneObservation& observation = track->observations[o];
			CHECK(observation.view == cameras[o]);
			// Exact: every frame put it at one pixel, and the median of equal values is that value.
			CHECK(math::length(observation.pixel - *scene::pixelOf(cameras[o], l, 0)) < 1e-9);
			CHECK(observation.support == 5);
			REQUIRE(observation.covariance.has_value());
			CHECK(std::abs((*observation.covariance)[0] - 0.136 * 0.136) < 1e-12); // 3.4% of a 4 px feature
			CHECK((*observation.covariance)[1] == 0.0);
		}
	}
	CHECK(set.tracks.size() == expected);
	CHECK(result.report.tracks == expected);
	CHECK(result.report.conflicts == 0);

	// Identities are SHA-256 hex, unique, and the order the tracks are in.
	for (std::size_t t = 0; t < set.tracks.size(); ++t)
	{
		CHECK(set.tracks[t].identity.size() == 64);
		if (t > 0)
			CHECK(set.tracks[t - 1].identity < set.tracks[t].identity);
	}

	// Every pair shares landmarks, so every pair verifies.
	REQUIRE(result.report.pairs.size() == 6);
	for (const PairExtraction& p : result.report.pairs)
	{
		CHECK(p.outcome == PairOutcome::Verified);
		CHECK(p.inliers == p.matches);
	}
	CHECK(result.report.extractor.backend == "truth");
	CHECK(result.report.matcher.backend == "exact");
	CHECK(result.report.geometry.backend == "truth");
	for (const CameraExtraction& c : result.report.cameras)
	{
		CHECK(c.frames == 5);
		CHECK(c.scale == 1.0);
		CHECK(c.transient == 0);
		CHECK(c.orientations == 0);
		CHECK(c.duplicates == 0);
	}

	SECTION("searched at half scale, an observation's covariance follows its size, not the scale")
	{
		// The stand-in finds every landmark at 4 source px whatever the scale, as a scale-invariant
		// detector finds a feature at its own size: the covariance says how large the feature is, and
		// a coarser search changes it only by finding features larger.
		ExtractionRequest request;
		request.scale = LongestSide{320};
		const ExtractionResult half = run(request);
		REQUIRE(half.ok());
		CHECK(half.report.cameras.front().scale == 0.5);
		REQUIRE_FALSE(half.trackSet.tracks.empty());
		CHECK(std::abs((*half.trackSet.tracks.front().observations.front().covariance)[0] - 0.136 * 0.136) < 1e-12);
	}
}

TEST_CASE("moving content is dropped before matching", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	for (std::size_t l = 0; l < 10; ++l)
		scene::scene().landmarks[l].dynamic = true;
	const ExtractionResult result = run();
	REQUIRE(result.ok());

	// Each moving landmark is matched between frames but found somewhere else every time, so it never
	// reaches half of a camera's frames.
	for (std::size_t l = 0; l < 10; ++l)
		CHECK_FALSE(anyTrackHolds(result.trackSet, l));
	std::size_t expected = 0;
	for (std::size_t l = 10; l < scene::scene().landmarks.size(); ++l)
		expected += staticIn(l, {0, 2, 4, 6, 8}).size() >= 2 ? 1 : 0;
	CHECK(result.trackSet.tracks.size() == expected);
	for (std::size_t c = 0; c < 4; ++c)
	{
		// Every sighting of a moving landmark, in the frames sampled.
		std::uint32_t sightings = 0;
		for (std::size_t l = 0; l < 10; ++l)
		{
			for (const std::size_t k : {0, 2, 4, 6, 8})
				sightings += scene::pixelOf(c, l, k) ? 1 : 0;
		}
		CHECK(sightings > 0);
		CHECK(result.report.cameras[c].transient == sightings);
	}
}

TEST_CASE("a feature hidden in a minority of frames stays static, and in a majority does not", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	REQUIRE(scene::pixelOf(0, 5, 0));
	REQUIRE(scene::pixelOf(0, 6, 0));
	scene::scene().landmarks[5].hidden = { {0, 0},
										   {0, 2} }; // 2 of camera 0's 5 sampled frames
	scene::scene().landmarks[6].hidden = { {0, 0},
										   {0, 2},
										   {0, 4} }; // 3 of them
	const ExtractionResult result = run();
	REQUIRE(result.ok());

	const Track* minority = trackAt(result.trackSet, 0, *scene::pixelOf(0, 5, 4));
	REQUIRE(minority != nullptr);
	CHECK(minority->observations.front().view == 0);
	CHECK(minority->observations.front().support == 3);

	// Landmark 6 still joins the other cameras, without camera 0.
	CHECK(trackAt(result.trackSet, 0, *scene::pixelOf(0, 6, 4)) == nullptr);
	const Track* majority = trackAt(result.trackSet, 1, *scene::pixelOf(1, 6, 4));
	REQUIRE(majority != nullptr);
	CHECK(majority->observations.front().view == 1);
	CHECK(result.report.cameras[0].transient == 2); // its two remaining sightings
}

TEST_CASE("one sampled frame keeps every feature", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	scene::scene().landmarks[0].dynamic = true;
	ExtractionRequest request;
	request.samples = 1;
	const ExtractionResult result = run(request);
	REQUIRE(result.ok());
	CHECK(ordinalsOf(result.trackSet) == std::vector<std::size_t>{4});

	// With nothing to compare it with, the moving landmark is kept, and two cameras saw it at one
	// instant, so its matches verify like any other point's.
	const std::vector<std::size_t> cameras = staticIn(0, {4});
	REQUIRE(cameras.size() >= 2);
	const Track* track = trackAt(result.trackSet, std::uint32_t(cameras.front()), *scene::pixelOf(cameras.front(), 0, 4));
	REQUIRE(track != nullptr);
	CHECK(track->observations.size() == cameras.size());
	CHECK(track->observations.front().support == 1);
}

TEST_CASE("a feature found at several orientations is one feature", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	for (std::size_t l = 0; l < 5; ++l)
		scene::scene().landmarks[l].duplicated = true;
	const ExtractionResult result = run();
	REQUIRE(result.ok());

	// Each orientation matches its counterpart in the other camera, and the two matches name one pair
	// of features, so each landmark is one track, held once per view.
	for (std::size_t l = 0; l < 5; ++l)
	{
		const std::vector<std::size_t> cameras = staticIn(l, {0, 2, 4, 6, 8});
		REQUIRE(cameras.size() >= 2);
		const Track* track = trackAt(result.trackSet, std::uint32_t(cameras.front()), *scene::pixelOf(cameras.front(), l, 0));
		REQUIRE(track != nullptr);
		CHECK(track->observations.size() == cameras.size());
	}
	for (std::size_t c = 0; c < 4; ++c)
	{
		std::uint32_t sightings = 0;
		for (std::size_t l = 0; l < 5; ++l)
		{
			for (const std::size_t k : {0, 2, 4, 6, 8})
				sightings += scene::pixelOf(c, l, k) ? 1 : 0;
		}
		CHECK(result.report.cameras[c].orientations == sightings);
		CHECK(result.report.cameras[c].duplicates == 0);
		std::uint32_t statics = 0;
		for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
		{
			const std::vector<std::size_t> cameras = staticIn(l, {0, 2, 4, 6, 8});
			statics += std::find(cameras.begin(), cameras.end(), c) != cameras.end() ? 1 : 0;
		}
		CHECK(result.report.cameras[c].staticFeatures == statics);
	}
	for (const PairExtraction& p : result.report.pairs)
	{
		CHECK(p.ambiguous == 0);
		CHECK(p.inliers == p.matches);
	}
}

TEST_CASE("an orientation one camera lacks still joins", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	// Camera 0 finds landmark 0 at angle 0 and again turned, at 1.5; the other cameras, rolled, find
	// only the turned orientation, at 0.2. Keeping one orientation per pixel before matching would
	// keep the angle-0 one in camera 0 and the turned one elsewhere, which no match joins. Matched
	// at every orientation, camera 0's turned one finds the others'.
	scene::Landmark& landmark = scene::scene().landmarks[0];
	landmark.duplicated = true;
	landmark.turnedOnly = {1, 2, 3};
	const std::vector<std::size_t> cameras = staticIn(0, {0, 2, 4, 6, 8});
	REQUIRE(cameras.size() >= 2);
	REQUIRE(cameras.front() == 0);
	const ExtractionResult result = run();
	REQUIRE(result.ok());
	const Track* track = trackAt(result.trackSet, 0, *scene::pixelOf(0, 0, 0));
	REQUIRE(track != nullptr);
	CHECK(track->observations.size() == cameras.size());
}

TEST_CASE("a feature matched to two of another camera's is dropped from that pair", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	scene::Scene& s = scene::scene();
	// Landmark L, and M placed in the epipolar plane of cameras 0 and 1 through L, so camera 0's L and
	// camera 1's M agree with the pair's relative pose. Camera 0 finds a second orientation of L, and
	// camera 1 one of M, described exactly as camera 0 describes L's. So camera 0's L matches camera
	// 1's L (first orientations) and M (second), and camera 1's M matches camera 0's M and L. Joined,
	// all of it would be one component holding two of camera 0's features: a conflict, rejected whole.
	scene::Landmark& l = s.landmarks[0];
	scene::Landmark& m = s.landmarks[1];
	const math::Vec3d c0 = s.referenceFromCamera[0].translation();
	const math::Vec3d c1 = s.referenceFromCamera[1].translation();
	m.point = l.point + 0.2 * (c1 - c0) + 0.1 * (l.point - c0);
	l.turnedIn = {{0, l.descriptor + scene::kTurned}};
	m.turnedIn = {{1, l.descriptor + scene::kTurned}};
	REQUIRE(staticIn(0, {0, 2, 4, 6, 8}).size() == 4);
	REQUIRE(staticIn(1, {0, 2, 4, 6, 8}).size() == 4);

	const ExtractionResult result = run();
	REQUIRE(result.ok());
	// Cameras 0 and 1 drop (L, L), (L, M) and (M, M): L of camera 0 and M of camera 1 each matched
	// two features. No other pair sees a second orientation on both sides.
	REQUIRE(result.report.pairs.size() == 6);
	CHECK(result.report.pairs[0].a == 0);
	CHECK(result.report.pairs[0].b == 1);
	CHECK(result.report.pairs[0].ambiguous == 3);
	for (std::size_t p = 1; p < result.report.pairs.size(); ++p)
		CHECK(result.report.pairs[p].ambiguous == 0);
	CHECK(result.report.conflicts == 0);
	// Both still join through the other cameras, each a track in every view.
	for (const std::size_t landmark : {0, 1})
	{
		const Track* track = trackAt(result.trackSet, 0, *scene::pixelOf(0, landmark, 0));
		REQUIRE(track != nullptr);
		CHECK(track->observations.size() == 4);
	}
}

TEST_CASE("a conflicting track is rejected whole", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset(3);
	scene::Scene& s = scene::scene();
	// Landmark L, which the three cameras describe a little differently (100, 101, 109), and D, seen
	// by camera 0 alone and described as 110, on camera 2's ray through L. Cameras 0 and 1 join L;
	// cameras 1 and 2 join L; and camera 2's L is nearer D than camera 0's L, so cameras 0 and 2 join
	// L with D, which verifies, since D is where camera 2's ray goes. The component holds two of
	// camera 0's features.
	scene::Landmark& l = s.landmarks[0];
	l.cameras = {0, 1, 2};
	l.descriptorIn = {{0, 100}, {1, 101}, {2, 109}};
	scene::Landmark d;
	const math::Vec3d c2 = s.referenceFromCamera[2].translation();
	d.point = c2 + 1.4 * (l.point - c2);
	d.descriptor = 110;
	d.cameras = {0};
	s.landmarks.push_back(d);
	const std::size_t dIndex = s.landmarks.size() - 1;
	for (std::size_t c = 0; c < 3; ++c)
		REQUIRE(scene::pixelOf(c, 0, 0));
	REQUIRE(scene::pixelOf(0, dIndex, 0));

	const ExtractionResult result = run();
	REQUIRE(result.ok());
	CHECK_FALSE(anyTrackHolds(result.trackSet, 0));
	CHECK_FALSE(anyTrackHolds(result.trackSet, dIndex));
	CHECK(result.report.conflicts == 1);
	CHECK(result.report.conflictObservations == 4);
	// Everything else is still a track.
	std::size_t expected = 0;
	for (std::size_t i = 1; i < dIndex; ++i)
		expected += staticIn(i, {0, 2, 4, 6, 8}).size() >= 2 ? 1 : 0;
	CHECK(result.trackSet.tracks.size() == expected);
}

TEST_CASE("input order and execution policy do not change the track set", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset(5, 80, 7);
	for (std::size_t l = 0; l < 8; ++l)
		scene::scene().landmarks[l].dynamic = true;
	for (std::size_t l = 8; l < 12; ++l)
		scene::scene().landmarks[l].duplicated = true;
	scene::scene().landmarks[12].hidden = { {1, 2} };

	const std::vector<RigFootage> cameras = scene::footage();
	const std::vector<capture::CaptureGroup> groups = scene::groups(cameras);
	ExtractionRequest serial;
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const ExtractionResult reference = extractTracks(cameras, groups, serial);
	REQUIRE(reference.ok());
	REQUIRE(reference.trackSet.tracks.size() > 40);

	SECTION("reversed")
	{
		const std::vector<RigFootage> reversedCameras(cameras.rbegin(), cameras.rend());
		const std::vector<capture::CaptureGroup> reversedGroups(groups.rbegin(), groups.rend());
		const ExtractionResult reversed = extractTracks(reversedCameras, reversedGroups, serial);
		REQUIRE(reversed.ok());
		requireEqual(reference.trackSet, reversed.trackSet);
	}
	SECTION("in parallel")
	{
		const lain::testing::ThreadPool pool{4};
		const ExtractionResult parallel = extractTracks(cameras, groups, ExtractionRequest{});
		REQUIRE(parallel.ok());
		requireEqual(reference.trackSet, parallel.trackSet);
		for (std::size_t p = 0; p < reference.report.pairs.size(); ++p)
			CHECK(parallel.report.pairs[p].inliers == reference.report.pairs[p].inliers);
	}
}

TEST_CASE("each sampled frame is decoded exactly once", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	const std::vector<RigFootage> cameras = scene::footage();
	std::vector<capture::CaptureGroup> groups = scene::groups(cameras);
	std::rotate(groups.begin(), groups.begin() + 4, groups.end());
	std::reverse(groups.begin(), groups.end());
	ExtractionRequest request;
	request.samples = 3;
	const ExtractionResult result = extractTracks(cameras, groups, request);
	REQUIRE(result.ok());

	// Three of nine groups, at the centres of three equal bins, whatever order they came in.
	CHECK(ordinalsOf(result.trackSet) == std::vector<std::size_t>{1, 4, 7});
	std::map<std::pair<std::size_t, std::size_t>, std::size_t> expected;
	for (std::size_t c = 0; c < 4; ++c)
	{
		for (const std::size_t k : {1, 4, 7})
			expected[{c, k}] = 1;
	}
	CHECK(scene::scene().decodes == expected);
}

TEST_CASE("a pair with no overlap is skipped by the pre-screen", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset(3, 40);
	scene::Scene& s = scene::scene();
	// Cameras 0 and 1 see the first twenty landmarks, camera 2 the other twenty, described so far
	// from the first twenty that nothing of camera 2 matches them.
	for (std::size_t l = 0; l < 40; ++l)
	{
		if (l < 20)
			s.landmarks[l].cameras = {0, 1};
		else
		{
			s.landmarks[l].cameras = {2};
			s.landmarks[l].descriptor = 10'000'000 + std::uint32_t(1000 * l);
		}
	}
	ExtractionRequest request;
	request.prescreenFeatures = 8;
	const ExtractionResult result = run(request);
	REQUIRE(result.ok());
	REQUIRE(result.report.cameras[0].staticFeatures > 8);
	REQUIRE(result.report.cameras[2].staticFeatures > 8);

	REQUIRE(result.report.pairs.size() == 3);
	CHECK(result.report.pairs[0].outcome == PairOutcome::Verified); // 0-1
	CHECK(result.report.pairs[0].prescreenMatches == 8);
	for (const std::size_t p : {1, 2}) // 0-2, 1-2
	{
		CHECK(result.report.pairs[p].outcome == PairOutcome::Skipped);
		CHECK(result.report.pairs[p].prescreenMatches == 0);
		CHECK(result.report.pairs[p].matches == 0);
	}
	// No full match ran between camera 2 and another camera: every call across the two sets was a
	// pre-screen of eight.
	for (const scene::Scene::Call& call : s.nearestCalls)
	{
		const bool across = (call.firstA >= 10'000'000) != (call.firstB >= 10'000'000);
		if (across)
		{
			CHECK(call.sizeA == 8);
			CHECK(call.sizeB == 8);
		}
	}
}

TEST_CASE("a pair whose matches do not verify joins nothing", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	// Camera 3's frames were taken tilted 0.2 rad from where the geometry solver knows it is. Measured:
	// no candidate explains one of its 60 matches with any camera. A 0.05 rad turn about the vertical
	// with a 0.2 m shift left 37 to 47 of 60 inliers: that is nearly an orbit about the scene, which
	// keeps most points on their epipolar lines, and a relative pose tests nothing else.
	scene::scene().renderedFrom[3] =
		scene::scene().referenceFromCamera[3] *
		math::RigidTransformd { math::angleAxis(0.2, math::Vec3d{1, 0, 0}),
								math::Vec3d{0.0} };
	const ExtractionResult result = run();
	REQUIRE(result.ok());
	for (const PairExtraction& p : result.report.pairs)
	{
		CAPTURE(p.a, p.b, p.detail);
		CHECK(p.outcome == (p.b == 3 ? PairOutcome::Unverified : PairOutcome::Verified));
		if (p.b == 3)
		{
			CHECK(p.inliers == 0);
			CHECK_THAT(p.detail, ContainsSubstring("explains 0 of 60"));
		}
	}
	for (const Track& t : result.trackSet.tracks)
	{
		for (const SceneObservation& o : t.observations)
			CHECK(o.view != 3);
	}
	CHECK_FALSE(result.trackSet.tracks.empty());
}

TEST_CASE("extraction refuses what it cannot run before decoding anything", "[camera][feature][tracks]")
{
	scene::registerStandIns();
	scene::reset();
	const std::vector<RigFootage> cameras = scene::footage();
	const std::vector<capture::CaptureGroup> groups = scene::groups(cameras);
	const auto refuses = [](const ExtractionResult& r, Status status, const std::string& reason)
	{
		CHECK(r.status == status);
		CHECK_THAT(r.detail, ContainsSubstring(reason));
		CHECK(r.trackSet.tracks.empty());
		CHECK(scene::scene().decodes.empty());
	};

	SECTION("approximate search under deterministic debugging")
	{
		ExtractionRequest request;
		request.matching.search = MatchSearch::Approximate;
		request.execution = ExecutionPolicy::DeterministicDebug;
		refuses(extractTracks(cameras, groups, request), Status::Unsupported, "reproducible only within a tolerance");
	}
	SECTION("a search no matcher runs")
	{
		scene::scene().approximate = false;
		ExtractionRequest request;
		request.matching.search = MatchSearch::Approximate;
		refuses(extractTracks(cameras, groups, request), Status::Unsupported, "no matcher supports Approximate");
	}
	SECTION("a request out of range")
	{
		ExtractionRequest ratio;
		ratio.matching.ratio = 0;
		refuses(extractTracks(cameras, groups, ratio), Status::Unsupported, "ratio");
		ExtractionRequest samples;
		samples.samples = 0;
		refuses(extractTracks(cameras, groups, samples), Status::Unsupported, "sample count");
		ExtractionRequest inliers;
		inliers.minimumPairInliers = 4;
		refuses(extractTracks(cameras, groups, inliers), Status::Unsupported, "at least 5 inliers");
		ExtractionRequest localisation;
		localisation.localisationPerSize = 0;
		refuses(extractTracks(cameras, groups, localisation), Status::Unsupported, "per unit of size");
	}
	SECTION("too little to join")
	{
		refuses(extractTracks({cameras.front()}, groups), Status::TooFew, "a track joins two");
		refuses(extractTracks(cameras, {}), Status::TooFew, "no capture group");
	}
	SECTION("a malformed dataset")
	{
		refuses(extractTracks({cameras[0], cameras[1], cameras[0]}, groups), Status::InvalidInput, "more than once");

		std::vector<RigFootage> unnamed = cameras;
		unnamed[2].camera = {};
		refuses(extractTracks(unnamed, groups), Status::InvalidInput, "no identity");

		const std::vector<RigFootage> three(cameras.begin(), cameras.begin() + 3);
		refuses(extractTracks(three, groups), Status::InvalidInput, "which was not given");

		media::FrameRef missing = cameras[0].footage.frame(0);
		missing.ordinal = 99;
		const capture::CaptureGroup beyond = *capture::CaptureGroup::create({{cameras[0].camera, missing}}).group;
		refuses(extractTracks(cameras, {beyond}), Status::InvalidInput, "footage has no");

		std::vector<RigFootage> resized = cameras;
		CameraModelParameters p = camera::testing::parametersWith(camera::testing::brownConrady());
		p.image = {800, 600};
		resized[1].model = *CameraModel::create(p).model;
		refuses(extractTracks(resized, groups), Status::InvalidInput, "800x600");
	}
}
