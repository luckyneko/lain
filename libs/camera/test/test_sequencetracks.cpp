// Feature-track extraction for one moving camera (feature::extractSequenceTracks) over stand-ins answering
// from a known scene: every sampled frame its own view, pairs of frames verified by a fundamental matrix,
// and none of it depending on the order the footage lists its frames in or on the execution policy.
// Every count is the scene's own, worked out beside its check.

#include "syntheticscene.h"

#include <lain/camera/feature/sequenceextraction.h>
#include <lain/media/operations.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
namespace scene = lain::camera::testing::scene;
using Catch::Matchers::ContainsSubstring;

namespace
{
	// The moving camera is camera 0 of the scene.
	std::optional<math::Vec2d> pixelAt(std::size_t l, std::size_t ordinal)
	{
		return scene::pixelOf(0, l, ordinal);
	}

	// The ordinals of the frames a track set's views are, in view order.
	std::vector<std::size_t> ordinalsOf(const TrackSet& set)
	{
		std::vector<std::size_t> out;
		for (const View& v : set.views)
			out.push_back(v.frames.empty() ? std::numeric_limits<std::size_t>::max() : v.frames.front().ordinal);
		return out;
	}

	// The sampled frames that see landmark `l`, as view indices.
	std::vector<std::uint32_t> viewsSeeing(std::size_t l, const std::vector<std::size_t>& ordinals)
	{
		std::vector<std::uint32_t> out;
		for (std::uint32_t v = 0; v < ordinals.size(); ++v)
		{
			if (pixelAt(l, ordinals[v]))
				out.push_back(v);
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

	// Whether any track holds landmark `l` where any sampled frame saw it.
	bool anyTrackHolds(const TrackSet& set, std::size_t l)
	{
		const std::vector<std::size_t> ordinals = ordinalsOf(set);
		for (std::uint32_t v = 0; v < ordinals.size(); ++v)
		{
			if (const std::optional<math::Vec2d> pixel = pixelAt(l, ordinals[v]); pixel && trackAt(set, v, *pixel))
				return true;
		}
		return false;
	}

	SequenceExtractionResult run(const SequenceExtractionRequest& request = {})
	{
		return extractSequenceTracks(scene::sequenceFootage(), request);
	}

	void requireEqual(const TrackSet& a, const TrackSet& b)
	{
		CHECK(a.groups.empty());
		CHECK(b.groups.empty());
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

TEST_CASE("a moving camera gives one track per landmark two sampled frames see, exactly", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(12);
	SequenceExtractionRequest request;
	request.samples = 6;
	const SequenceExtractionResult result = run(request);
	REQUIRE(result.ok());
	const TrackSet& set = result.trackSet;

	// Six of twelve frames, at the centres of six equal bins, each its own view of the one camera.
	const std::vector<std::size_t> ordinals{1, 3, 5, 7, 9, 11};
	CHECK(ordinalsOf(set) == ordinals);
	CHECK(set.groups.empty());
	REQUIRE(set.views.size() == 6);
	for (const View& v : set.views)
	{
		CHECK(v.camera.value == "cam00");
		CHECK(v.image.width == 640);
		CHECK(v.image.height == 480);
		REQUIRE(v.frames.size() == 1);
		CHECK(v.frames.front().source.toString() == scene::sourceOf(0));
	}

	std::size_t expected = 0;
	for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
	{
		const std::vector<std::uint32_t> views = viewsSeeing(l, ordinals);
		if (views.size() < 2)
			continue;
		++expected;
		const Track* track = trackAt(set, views.front(), *pixelAt(l, ordinals[views.front()]));
		REQUIRE(track != nullptr);
		REQUIRE(track->observations.size() == views.size());
		for (std::size_t o = 0; o < views.size(); ++o)
		{
			const SceneObservation& observation = track->observations[o];
			CHECK(observation.view == views[o]);
			CHECK(math::length(observation.pixel - *pixelAt(l, ordinals[views[o]])) < 1e-9);
			CHECK(observation.support == 1);
			REQUIRE(observation.covariance.has_value());
			CHECK(std::abs((*observation.covariance)[0] - 0.136 * 0.136) < 1e-12); // 3.4% of a 4 px feature
			CHECK((*observation.covariance)[1] == 0.0);
			CHECK((*observation.covariance)[2] == (*observation.covariance)[0]);
		}
	}
	REQUIRE(expected > 40);
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

	// Every frame pair shares landmarks, so every pair verifies, and a static scene's every match is
	// explained by the pair's true fundamental matrix.
	REQUIRE(result.report.frames.size() == 6);
	for (std::size_t v = 0; v < 6; ++v)
	{
		const FrameExtraction& f = result.report.frames[v];
		CHECK(f.scale == 1.0);
		CHECK_FALSE(f.failed);
		CHECK(f.capped == 0);
		CHECK(f.orientations == 0);
		std::uint32_t seen = 0;
		for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
			seen += pixelAt(l, ordinals[v]) ? 1 : 0;
		CHECK(f.found == seen);
		CHECK(f.features == seen);
	}
	REQUIRE(result.report.pairs.size() == 15);
	std::size_t p = 0;
	for (std::uint32_t a = 0; a < 6; ++a)
	{
		for (std::uint32_t b = a + 1; b < 6; ++b, ++p)
		{
			const PairExtraction& pair = result.report.pairs[p];
			CAPTURE(a, b, pair.detail);
			CHECK(pair.a == a);
			CHECK(pair.b == b);
			CHECK(pair.outcome == PairOutcome::Verified);
			CHECK(pair.inliers == pair.matches);
			std::uint32_t shared = 0;
			for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
				shared += pixelAt(l, ordinals[a]) && pixelAt(l, ordinals[b]) ? 1 : 0;
			CHECK(pair.matches == shared);
		}
	}
	CHECK(result.report.extractor.backend == "truth");
	CHECK(result.report.matcher.backend == "exact");
	CHECK(result.report.geometry.backend == "truth");
	CHECK(result.report.request.samples == 6);
}

TEST_CASE("a sequence's verification threshold is the request's, in source pixels", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(12);
	SequenceExtractionRequest request;
	request.samples = 4;

	// Six pairs of four frames, each verified once. The request states 4 px in the pixels searched:
	// natively that is 4 source pixels, and searched at half scale, where a searched pixel spans two
	// source pixels, it is 8.
	SECTION("native")
	{
		const SequenceExtractionResult result = run(request);
		REQUIRE(result.ok());
		CHECK(scene::scene().fundamentalThresholds == std::vector<double>(6, 4.0));
	}
	SECTION("at half scale")
	{
		request.scale = LongestSide{320};
		const SequenceExtractionResult result = run(request);
		REQUIRE(result.ok());
		for (const FrameExtraction& f : result.report.frames)
			CHECK(f.scale == 0.5);
		CHECK(scene::scene().fundamentalThresholds == std::vector<double>(6, 8.0));
		// The covariance follows a feature's size, which is in source pixels whatever the scale.
		REQUIRE_FALSE(result.trackSet.tracks.empty());
		CHECK(std::abs((*result.trackSet.tracks.front().observations.front().covariance)[0] - 0.136 * 0.136) < 1e-12);
	}
}

TEST_CASE("moving content verifies in no frame pair and joins no track", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(12);
	// Ten landmarks rising 10 cm a frame, across the camera's mostly sideways travel, so they leave
	// their epipolar lines. Each is matched between frames, since it looks the same in every frame.
	for (std::size_t l = 0; l < 10; ++l)
	{
		scene::scene().landmarks[l].dynamic = true;
		scene::scene().landmarks[l].velocity = math::Vec3d { 0.0,
															 0.1,
															 0.0 };
	}
	SequenceExtractionRequest request;
	request.samples = 6;
	const SequenceExtractionResult result = run(request);
	REQUIRE(result.ok());
	const std::vector<std::size_t> ordinals{1, 3, 5, 7, 9, 11};

	// Worked out from the truth: per pair, the matches the pair's true fundamental matrix explains
	// within the threshold. Every static landmark is one, and no moving one is.
	REQUIRE(result.report.pairs.size() == 15);
	for (const PairExtraction& pair : result.report.pairs)
	{
		const math::Mat3d f = scene::fundamentalOf(ordinals[pair.a], ordinals[pair.b]);
		std::uint32_t shared = 0, explained = 0, moving = 0;
		for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
		{
			const std::optional<math::Vec2d> a = pixelAt(l, ordinals[pair.a]);
			const std::optional<math::Vec2d> b = pixelAt(l, ordinals[pair.b]);
			if (!a || !b)
				continue;
			++shared;
			const bool inlier = scene::sampsonOf(f, PixelPair{*a, *b}) <= 4.0;
			explained += inlier ? 1 : 0;
			if (l < 10)
			{
				++moving;
				CHECK_FALSE(inlier);
			}
		}
		CAPTURE(pair.a, pair.b);
		REQUIRE(moving > 0);
		CHECK(pair.outcome == PairOutcome::Verified);
		CHECK(pair.matches == shared);
		CHECK(pair.inliers == explained);
		CHECK(pair.inliers == shared - moving);
	}
	for (std::size_t l = 0; l < 10; ++l)
		CHECK_FALSE(anyTrackHolds(result.trackSet, l));
	std::size_t expected = 0;
	for (std::size_t l = 10; l < scene::scene().landmarks.size(); ++l)
		expected += viewsSeeing(l, ordinals).size() >= 2 ? 1 : 0;
	CHECK(result.trackSet.tracks.size() == expected);
}

TEST_CASE("a frame pair with no overlap is skipped by the pre-screen", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(12, 40);
	scene::Scene& s = scene::scene();
	// The first half of the footage sees the first twenty landmarks, the second half the other twenty,
	// described so far from the first twenty that nothing of the second half matches them.
	for (std::size_t l = 0; l < 40; ++l)
	{
		for (std::size_t k = 0; k < 12; ++k)
		{
			if ((l < 20) != (k < 6))
				s.landmarks[l].hidden.insert({0, k});
		}
		if (l >= 20)
			s.landmarks[l].descriptor = 10'000'000 + std::uint32_t(1000 * l);
	}
	SequenceExtractionRequest request;
	request.samples = 6;
	request.prescreenFeatures = 8;
	const SequenceExtractionResult result = run(request);
	REQUIRE(result.ok());
	for (const FrameExtraction& f : result.report.frames)
		REQUIRE(f.features > 8);

	// Views 0 to 2 are frames 1, 3 and 5; views 3 to 5 are frames 7, 9 and 11.
	REQUIRE(result.report.pairs.size() == 15);
	for (const PairExtraction& pair : result.report.pairs)
	{
		CAPTURE(pair.a, pair.b);
		if ((pair.a < 3) == (pair.b < 3))
		{
			CHECK(pair.outcome == PairOutcome::Verified);
			CHECK(pair.prescreenMatches == 8);
		}
		else
		{
			CHECK(pair.outcome == PairOutcome::Skipped);
			CHECK(pair.prescreenMatches == 0);
			CHECK(pair.matches == 0);
		}
	}
	// No full match ran across the halves: every call across the two sets was a pre-screen of eight.
	for (const scene::Scene::Call& call : s.nearestCalls)
	{
		if ((call.firstA >= 10'000'000) != (call.firstB >= 10'000'000))
		{
			CHECK(call.sizeA == 8);
			CHECK(call.sizeB == 8);
		}
	}
	CHECK(s.fundamentalThresholds.size() == 6);
}

TEST_CASE("a conflicting sequence track is rejected whole", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(3);
	scene::Scene& s = scene::scene();
	// Landmark L, which the three frames describe a little differently (100, 101, 109), and D, seen in
	// frame 0 alone and described as 110, on frame 2's ray through L. Frames 0 and 1 join L; frames 1
	// and 2 join L; and frame 2's L is nearer D than frame 0's L, so frames 0 and 2 join L with D,
	// which verifies, since D is where frame 2's ray goes. The component holds two of frame 0's
	// features.
	scene::Landmark& l = s.landmarks[0];
	l.descriptorAt = {{0, 100}, {1, 101}, {2, 109}};
	scene::Landmark d;
	const math::Vec3d c2 = s.path[2].translation();
	d.point = c2 + 1.4 * (l.point - c2);
	d.descriptor = 110;
	d.hidden = {{0, 1},
				{0, 2}};
	s.landmarks.push_back(d);
	const std::size_t dIndex = s.landmarks.size() - 1;
	for (std::size_t k = 0; k < 3; ++k)
		REQUIRE(pixelAt(0, k));
	REQUIRE(pixelAt(dIndex, 0));

	// Three frames, so every one is sampled.
	const SequenceExtractionResult result = run();
	REQUIRE(result.ok());
	CHECK(ordinalsOf(result.trackSet) == std::vector<std::size_t>{0, 1, 2});
	CHECK_FALSE(anyTrackHolds(result.trackSet, 0));
	CHECK_FALSE(anyTrackHolds(result.trackSet, dIndex));
	CHECK(result.report.conflicts == 1);
	CHECK(result.report.conflictObservations == 4);
	// Everything else is still a track.
	std::size_t expected = 0;
	for (std::size_t i = 1; i < dIndex; ++i)
		expected += viewsSeeing(i, {0, 1, 2}).size() >= 2 ? 1 : 0;
	CHECK(result.trackSet.tracks.size() == expected);
}

TEST_CASE("a frame that does not decode is a view with no observations", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(12);
	scene::scene().undecodable = { {0, 5} };
	SequenceExtractionRequest request;
	request.samples = 6;
	const SequenceExtractionResult result = run(request);
	REQUIRE(result.ok());

	// Frame 5 is view 2: still a view, as a camera with no tracks is in a rig, but of no frame.
	REQUIRE(result.trackSet.views.size() == 6);
	CHECK(result.trackSet.views[2].camera.value == "cam00");
	CHECK(result.trackSet.views[2].frames.empty());
	REQUIRE(result.report.frames.size() == 6);
	for (std::size_t v = 0; v < 6; ++v)
	{
		CHECK(result.report.frames[v].failed == (v == 2));
		CHECK((result.report.frames[v].features == 0) == (v == 2));
	}
	for (const PairExtraction& pair : result.report.pairs)
	{
		CAPTURE(pair.a, pair.b, pair.detail);
		CHECK(pair.outcome == (pair.a == 2 || pair.b == 2 ? PairOutcome::TooFew : PairOutcome::Verified));
	}

	// Every other landmark that two of the five decoded frames see is a track, and none is in view 2.
	const std::vector<std::size_t> decoded{1, 3, 7, 9, 11};
	std::size_t expected = 0;
	for (std::size_t l = 0; l < scene::scene().landmarks.size(); ++l)
		expected += viewsSeeing(l, decoded).size() >= 2 ? 1 : 0;
	CHECK(result.trackSet.tracks.size() == expected);
	for (const Track& t : result.trackSet.tracks)
	{
		for (const SceneObservation& o : t.observations)
			CHECK(o.view != 2);
	}
}

TEST_CASE("frame order and execution policy do not change a sequence's track set", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(16, 80, 7);
	for (std::size_t l = 0; l < 8; ++l)
	{
		scene::scene().landmarks[l].dynamic = true;
		scene::scene().landmarks[l].velocity = math::Vec3d { 0.0,
															 0.1,
															 0.0 };
	}
	for (std::size_t l = 8; l < 12; ++l)
		scene::scene().landmarks[l].duplicated = true;
	scene::scene().landmarks[12].hidden = { {0, 5} };

	const capture::CameraFootage footage = scene::sequenceFootage();
	SequenceExtractionRequest serial;
	serial.samples = 7;
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const SequenceExtractionResult reference = extractSequenceTracks(footage, serial);
	REQUIRE(reference.ok());
	REQUIRE(reference.trackSet.tracks.size() > 40);

	SECTION("reversed")
	{
		// Listed backwards, the frames are put back in capture order before any is sampled.
		const capture::CameraFootage reversed{footage.camera, media::reverse(footage.footage)};
		const SequenceExtractionResult result = extractSequenceTracks(reversed, serial);
		REQUIRE(result.ok());
		requireEqual(reference.trackSet, result.trackSet);
	}
	SECTION("in parallel")
	{
		const lain::testing::ThreadPool pool{4};
		SequenceExtractionRequest parallel = serial;
		parallel.execution = SequenceExtractionRequest{}.execution;
		const SequenceExtractionResult result = extractSequenceTracks(footage, parallel);
		REQUIRE(result.ok());
		requireEqual(reference.trackSet, result.trackSet);
		REQUIRE(result.report.pairs.size() == reference.report.pairs.size());
		for (std::size_t p = 0; p < reference.report.pairs.size(); ++p)
			CHECK(result.report.pairs[p].inliers == reference.report.pairs[p].inliers);
	}
}

TEST_CASE("each sampled frame of a sequence is decoded exactly once", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(12);
	const capture::CameraFootage footage = scene::sequenceFootage();
	SequenceExtractionRequest request;
	request.samples = 3;
	const SequenceExtractionResult result =
		extractSequenceTracks(capture::CameraFootage{footage.camera, media::reverse(footage.footage)}, request);
	REQUIRE(result.ok());

	// Three of twelve frames, at the centres of three equal bins, whatever order they were listed in.
	CHECK(ordinalsOf(result.trackSet) == std::vector<std::size_t>{2, 6, 10});
	const std::map<std::pair<std::size_t, std::size_t>, std::size_t> expected{
		{{0, 2}, 1},
		{{0, 6}, 1},
		{{0, 10}, 1}};
	CHECK(scene::scene().decodes == expected);
}

TEST_CASE("a sequence track's identity is the digest of its pinned canonical text", "[camera][feature][sequence]")
{
	// Pinned against a digest computed outside lain, with the text spelled out, so a change to the text
	// (feature/sequenceextraction.cpp) has to be deliberate: it changes every identity a calibration
	// has reported.
	scene::registerStandIns();
	scene::resetMoving(2);
	const SequenceExtractionResult result = run();
	REQUIRE(result.ok());

	// Landmark 0's track: where each frame sees it, on the 1/64-pixel grid the text is spelled on.
	const math::Vec2d in0 = *pixelAt(0, 0);
	const math::Vec2d in1 = *pixelAt(0, 1);
	CHECK(std::llround(in0.x * 64.0) == 12635);
	CHECK(std::llround(in0.y * 64.0) == 10355);
	CHECK(std::llround(in1.x * 64.0) == 13434);
	CHECK(std::llround(in1.y * 64.0) == 10693);
	const Track* track = trackAt(result.trackSet, 0, in0);
	REQUIRE(track != nullptr);
	REQUIRE(track->observations.size() == 2);

	// The text, a version line and then each observation's camera and its frame's source (each
	// length-prefixed), its frame's ordinal and its pixel cell:
	//   "lain sequence feature track 1\n5:cam00 12:/scene/cam00 0 12635 10355\n"
	//   "5:cam00 12:/scene/cam00 1 13434 10693\n"
	// and its SHA-256, computed by sha256sum and by Python's hashlib, which agree. One camera's two
	// frames, so only the ordinal tells the two observations apart.
	CHECK(track->identity == "d6003598ac716838e6b3bc7be31d3b9eea126d421d0644ffa8b65c5b7bb460a7");
}

TEST_CASE("a moving camera keeps fewer features a frame than the fixed rig", "[camera][feature][sequence]")
{
	// Measured in M9 slice 4, sub-slice 10e for a moving camera; the rig's is the repo owner's
	// decision of 2026-10-09. A constructor sets the sequence's, so a default-constructed request and
	// a value-initialised one must both carry it.
	CHECK(SequenceExtractionRequest().featureCap == 2048);
	const SequenceExtractionRequest braced{};
	CHECK(braced.featureCap == 2048);
	CHECK(ExtractionRequest{}.featureCap == 8192);
	CHECK(ExtractionSettings{}.featureCap == 8192);
}

TEST_CASE("sequence extraction refuses what it cannot run before decoding anything", "[camera][feature][sequence]")
{
	scene::registerStandIns();
	scene::resetMoving(12);
	const capture::CameraFootage footage = scene::sequenceFootage();
	const auto refuses = [](const SequenceExtractionResult& r, Status status, const std::string& reason)
	{
		CHECK(r.status == status);
		CHECK_THAT(r.detail, ContainsSubstring(reason));
		CHECK(r.trackSet.tracks.empty());
		CHECK(r.trackSet.views.empty());
		CHECK(scene::scene().decodes.empty());
	};

	SECTION("approximate search under deterministic debugging")
	{
		SequenceExtractionRequest request;
		request.matching.search = MatchSearch::Approximate;
		request.execution = ExecutionPolicy::DeterministicDebug;
		refuses(extractSequenceTracks(footage, request), Status::Unsupported, "reproducible only within a tolerance");
	}
	SECTION("a search no matcher runs")
	{
		scene::scene().approximate = false;
		SequenceExtractionRequest request;
		request.matching.search = MatchSearch::Approximate;
		refuses(extractSequenceTracks(footage, request), Status::Unsupported, "no matcher supports Approximate");
	}
	SECTION("a request out of range")
	{
		SequenceExtractionRequest samples;
		samples.samples = 1;
		refuses(extractSequenceTracks(footage, samples), Status::Unsupported, "no frame pair");
		SequenceExtractionRequest inliers;
		inliers.minimumPairInliers = 7;
		refuses(extractSequenceTracks(footage, inliers), Status::Unsupported, "at least 8 inliers, the fewest a fundamental matrix");
		SequenceExtractionRequest zero;
		zero.threshold = 0;
		refuses(extractSequenceTracks(footage, zero), Status::Unsupported, "inlier distance");
		SequenceExtractionRequest notANumber;
		notANumber.threshold = std::numeric_limits<double>::quiet_NaN();
		refuses(extractSequenceTracks(footage, notANumber), Status::Unsupported, "inlier distance");
		SequenceExtractionRequest ratio;
		ratio.matching.ratio = 0;
		refuses(extractSequenceTracks(footage, ratio), Status::Unsupported, "ratio");
	}
	SECTION("too little to join")
	{
		refuses(extractSequenceTracks({footage.camera, media::clip(footage.footage, 4, 1)}), Status::TooFew,
				"a track joins two");
	}
	SECTION("a malformed sequence")
	{
		refuses(extractSequenceTracks({capture::CameraIdentity{}, footage.footage}), Status::InvalidInput, "no identity");
		const std::optional<media::FrameSequence> twice = media::concat(footage.footage, media::clip(footage.footage, 4, 1));
		REQUIRE(twice);
		refuses(extractSequenceTracks({footage.camera, *twice}), Status::InvalidInput, "is given twice");
	}
}
