// What matching every pair of a moving camera's 40 sampled frames costs, Exact against Approximate and
// with the pre-screen against without. It was a measurement test_sequencefeatures.cpp held to a bound
// until 2026-11-12. That test keeps what lain's code guarantees (every pair matched and verified, none
// skipped); this prints what it costs. Reported rather than checked, since a time depends on the machine
// and on what else runs, and so does whether any bound on it holds.
//
// The footage is test_sequencefeatures.cpp's own, so a number printed here describes the case beside it.

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/feature/matching.h>
#include <lain/camera/feature/sequenceextraction.h>
#include <lain/camera/opencv/register.h>
#include <lain/camera/scalepolicy.h>
#include <lain/camera/testing/featurecells.h>
#include <lain/camera/testing/testcamera.h>
#include <lain/camera/testing/texturedscene.h>
#include <lain/core/time.h>
#include <lain/media/framesequence.h>
#include <lain/string/format.h>
#include <lain/testing/skip.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
namespace synthetic = lain::camera::synthetic;
using camera::testing::largeCamera;
using camera::testing::strongest;

static void ensureBackend()
{
	static const bool once = []
	{
		opencv::registerBackend();
		return true;
	}();
	(void)once;
}

static void line(const std::string& label, const std::string& value)
{
	std::cout << string::format("  {:<16} {}\n", label, value);
}

static double secondsSince(const core::Time& start)
{
	return (core::Time::now() - start).seconds();
}

TEST_CASE("the time to match every pair of 40 frames", "[camera][opencv][feature][bench]")
{
	lain::testing::skipInDebug("a Debug build's timings mean nothing");
	ensureBackend();
	// The 1920 x 1080 camera along the path over the fine texture, so the cap binds, every frame drawn on
	// the pool first so nothing below times a render.
	const CameraModel m = largeCamera(1920, 1080);
	synthetic::Scene scene = synthetic::defaultScene(1, 0.01);
	scene.noise = 1.0;
	const auto source = std::make_shared<synthetic::RenderedSource>("fine", scene, m, synthetic::arcPath(40), 1);
	{
		const lain::testing::ThreadPool pool{4};
		source->drawAll();
	}
	const capture::CameraFootage footage{capture::CameraIdentity{"fine"}, media::FrameSequence::over(source)};

	// One pair on one thread: frames 0 and 1 capped at 8192 features with all their orientations, and
	// the pre-screen on the 512 strongest of those, both directions as match() asks.
	//
	// Last measured on linux-x86_64, Release: 10756 and 10738 rows at the cap (1.31 a feature), 1070
	// and 1044 at the pre-screen. The pre-screen took 0.039 s Exact and 0.033 s Approximate; the full
	// match 5.59 s Exact (6332 matches) and 0.41 s Approximate (6278). Brute force grows with the rows
	// squared: slice 3's pair of 12182 rows took 7.07 s on the same machine, which scales to 5.51 s here.
	const auto capped = [&](std::size_t f)
	{
		const ExtractResult extracted = extract(footage.footage.image(f), LongestSide{1920});
		REQUIRE(extracted.ok());
		return strongest(extracted.features, 8192);
	};
	const Features a = capped(0), b = capped(1);
	const Features screenA = strongest(a, 512), screenB = strongest(b, 512);
	MatchRequest approximate;
	approximate.search = MatchSearch::Approximate;
	const auto seconds = [](const Features& x, const Features& y, const MatchRequest& request)
	{
		const core::Time start = core::Time::now();
		const MatchResult result = match(x, y, request);
		REQUIRE(result.ok());
		return secondsSince(start);
	};
	const double screenExact = seconds(screenA, screenB, MatchRequest{});
	const double screenApproximate = seconds(screenA, screenB, approximate);
	const double fullExact = seconds(a, b, MatchRequest{});
	const double fullApproximate = seconds(a, b, approximate);

	// Every pair of all 40 frames at a cap of 1024, on four workers: the production run, whose pre-screen
	// reads the 512 strongest of the capped features. Last measured: 0 pairs skipped, 780 verified, 2608
	// tracks, in 61 s.
	SequenceExtractionRequest request;
	request.samples = 40;
	request.featureCap = 1024;
	SequenceExtractionResult result;
	{
		const lain::testing::ThreadPool pool{4};
		result = extractSequenceTracks(footage, request);
	}
	REQUIRE(result.ok());
	std::size_t skipped = 0;
	for (const PairExtraction& p : result.report.pairs)
		skipped += p.outcome == PairOutcome::Skipped ? 1 : 0;

	// All 780 pairs at the default cap, none skipped on this path since every frame sees the box. Last
	// measured: 1.21 core-hours Exact without the pre-screen and 1.22 with it; 5.3 and 5.8 core-minutes
	// Approximate. The pre-screen pays for itself once more than 0.7% of pairs share nothing under Exact,
	// 8% under Approximate.
	const double pairs = 780.0;
	std::cout << "the time to match every pair of 40 frames\n";
	line("rows", string::format("{} and {} at the cap, {} and {} at the pre-screen", a.size(), b.size(), screenA.size(), screenB.size()));
	line("pre-screen", string::format("{:.3f} s exact, {:.3f} s approximate, one thread", screenExact, screenApproximate));
	line("full match", string::format("{:.2f} s exact, {:.2f} s approximate, one thread", fullExact, fullApproximate));
	line("780 pairs", string::format("exact {:.2f} core-hours, {:.2f} with the pre-screen; approximate {:.1f} core-minutes, {:.1f} with it",
									 pairs * fullExact / 3600.0, pairs * (fullExact + screenExact) / 3600.0, pairs * fullApproximate / 60.0,
									 pairs * (fullApproximate + screenApproximate) / 60.0));
	line("production run", string::format("{} pairs, {} skipped, {} tracks in {:.1f} s on four workers", result.report.pairs.size(), skipped,
										  result.trackSet.tracks.size(), result.report.elapsed.seconds()));
}
