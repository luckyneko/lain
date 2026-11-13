// What the OpenCV feature producer costs: an 8K frame searched at the default sizes, and exact against
// approximate matching at 8192 features a view. Each was a measurement test_features.cpp held to a
// bound until 2026-11-12. That test keeps what lain's code guarantees (the side searched, the matches
// found and recalled); these print what it costs. Reported rather than checked, since a time depends on
// the machine and on what else runs, and so does whether any bound on it holds.
//
// The scenes are test_features.cpp's own, so a number printed here describes the case beside it.

#include <lain/camera/feature/features.h>
#include <lain/camera/feature/matching.h>
#include <lain/camera/opencv/register.h>
#include <lain/camera/scalepolicy.h>
#include <lain/camera/testing/featurecells.h>
#include <lain/camera/testing/testcamera.h>
#include <lain/camera/testing/texturedscene.h>
#include <lain/core/time.h>
#include <lain/string/format.h>
#include <lain/testing/skip.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <utility>

#if defined(__GLIBC__)
#	include <fstream>
#	include <malloc.h>
#endif

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
namespace synthetic = lain::camera::synthetic;
using camera::testing::cells;
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

#if defined(__GLIBC__)
// This process's resident set, and its peak since the last resetPeak(), in bytes.
static double statusBytes(const std::string& field)
{
	std::ifstream in("/proc/self/status");
	std::string text;
	while (std::getline(in, text))
		if (text.rfind(field + ":", 0) == 0)
			return std::stod(text.substr(field.size() + 1)) * 1024.0;
	return 0;
}

// Hands freed heap memory back first, or what the allocator kept resident would be reused without
// raising the peak.
static bool resetPeak()
{
	malloc_trim(0);
	std::ofstream out("/proc/self/clear_refs");
	out << "5";
	return bool(out);
}
#endif

TEST_CASE("an 8K frame searched at the default sizes", "[camera][opencv][feature][bench]")
{
	lain::testing::skipInDebug("a Debug build's timings mean nothing");
	ensureBackend();
	// SIFT's first octave doubles the searched image and keeps a float pyramid of it, so the search, not
	// the frame, sets the memory: the peak is per frame searched at once, on top of the decoded frame
	// (33 MB of 8K grey here, 100 MB of 8K RGB8).
	//
	// Last measured on linux-x86_64, Release: 0.54 s and 340 MB of peak growth at 1600 px, 0.60 s and
	// 489 MB at 1920 px (WORK.md, M9 slice 3, sub-slice 7).
	const CameraModel m = largeCamera(7680, 4320);
	core::Time start = core::Time::now();
	const image::Image frame = synthetic::render(synthetic::defaultScene(), m, synthetic::arcCamera(0.0), 0, 1, 1);
	std::cout << "an 8K frame searched at the default sizes, OpenCV on one thread\n";
	line("render", string::format("{:.1f} s", secondsSince(start)));
	for (const std::uint32_t side : {1600u, 1920u})
	{
#if defined(__GLIBC__)
		const bool peaks = resetPeak();
		const double before = statusBytes("VmRSS");
#endif
		start = core::Time::now();
		const ExtractResult found = extract(frame, LongestSide{side});
		const double seconds = secondsSince(start);
		REQUIRE(found.ok());
		std::string memory = "peak not measured (it reads /proc/self, glibc only)";
#if defined(__GLIBC__)
		if (peaks)
			memory = string::format("peak growth {:.0f} MB", (statusBytes("VmHWM") - before) / 1e6);
#endif
		line(string::format("at {} px", side), string::format("{} features in {:.2f} s, {}", found.features.size(), seconds, memory));
	}
}

TEST_CASE("exact and approximate search at 8192 features a view", "[camera][opencv][feature][bench]")
{
	lain::testing::skipInDebug("a Debug build's timings mean nothing");
	ensureBackend();
	// Two 1920 x 1080 views 0.3 rad apart of a finely textured scene, capped at the 8192 strongest
	// features each with all their orientations, as extractTracks caps a frame, and a third frame of
	// the first view with other noise, which is what consolidating one camera's frames matches.
	// match() is one thread whatever the pool, and asks both directions.
	//
	// Last measured on linux-x86_64, Release: 12182 and 12137 rows; Exact 3683 matches in 10.2 s,
	// Approximate 3637 in 0.78 s; consolidation (Exact) 10816 in 10.5 s; 16.9 core-hours Exact for 100
	// cameras, 4.0 with Approximate for the pairs, 1.5 with Approximate throughout. Brute force grows
	// with the rows, so keeping every orientation costs 2.8 times the 3.68 s of 8192 rows (WORK.md,
	// Sub-slice 7 follow-up B).
	const CameraModel m = largeCamera(1920, 1080);
	synthetic::Scene scene = synthetic::defaultScene(1, 0.01);
	scene.noise = 1.0;
	const auto features = [&](double angle, std::uint64_t seed)
	{
		const ExtractResult found = extract(synthetic::render(scene, m, synthetic::arcCamera(angle), 0, 2, seed), NativeScale{});
		REQUIRE(found.ok());
		return strongest(found.features, 8192);
	};
	const Features fa = features(-0.15, 1), fb = features(0.15, 2), again = features(-0.15, 3);
	REQUIRE(cells(fa).size() == 8192);
	REQUIRE(cells(fb).size() == 8192);

	MatchRequest approximateRequest;
	approximateRequest.search = MatchSearch::Approximate;
	const auto timed = [](const Features& x, const Features& y, const MatchRequest& request)
	{
		const core::Time start = core::Time::now();
		MatchResult result = match(x, y, request);
		const double seconds = secondsSince(start);
		REQUIRE(result.ok());
		return std::make_pair(std::move(result), seconds);
	};
	const auto [exact, exactSeconds] = timed(fa, fb, MatchRequest{});
	const auto [approximate, approximateSeconds] = timed(fa, fb, approximateRequest);
	const auto [consolidation, consolidationSeconds] = timed(fa, again, MatchRequest{});
	const auto [approximateConsolidation, approximateConsolidationSeconds] = timed(fa, again, approximateRequest);

	std::set<std::pair<std::uint32_t, std::uint32_t>> found;
	for (const Match& mt : exact.matches)
		found.insert({mt.a, mt.b});
	std::size_t recalled = 0;
	for (const Match& mt : approximate.matches)
		recalled += found.count({mt.a, mt.b});

	// For 100 cameras whose every pair overlaps: 4950 pair matches, and 1000 consolidation matches (each
	// camera's 5 sampled frames matched pairwise, 10 a camera). Brute force costs the same whatever the
	// content, so a consolidation match costs a pair match.
	const auto coreHours = [](double pair, double consolidate)
	{ return (4950.0 * pair + 1000.0 * consolidate) / 3600.0; };
	std::cout << "exact and approximate search at 8192 features a view, one thread\n";
	line("rows", string::format("{} and {}, {:.2f} a feature", fa.size(), fb.size(), double(fa.size()) / 8192.0));
	line("exact", string::format("{} matches in {:.2f} s", exact.matches.size(), exactSeconds));
	line("approximate", string::format("{} matches in {:.2f} s, {:.1f} times faster, {:.1f}% of exact's", approximate.matches.size(),
									   approximateSeconds, exactSeconds / approximateSeconds,
									   100.0 * double(recalled) / double(exact.matches.size())));
	line("consolidation", string::format("{} matches in {:.2f} s exact, {} in {:.2f} s approximate", consolidation.matches.size(),
										 consolidationSeconds, approximateConsolidation.matches.size(), approximateConsolidationSeconds));
	line("100 cameras", string::format("{:.1f} core-hours exact, {:.1f} approximate for the pairs, {:.1f} approximate throughout",
									   coreHours(exactSeconds, consolidationSeconds), coreHours(approximateSeconds, consolidationSeconds),
									   coreHours(approximateSeconds, approximateConsolidationSeconds)));
}
