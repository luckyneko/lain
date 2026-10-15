// A moving camera's footage through the OpenCV feature producer: drawn in plain C++ along a path
// (texturedscene.h's arcPath), so every track, match and matrix is judged against a known truth.
// Targetless calibration's evidence, measured before a model exists (WORK.md, M9 slice 4,
// sub-slice 8). Every bound is a measurement, written beside its check; the cases marked Release only
// also record what sub-slice 10 decides on (the threshold, the sampled frames, the cap).

#include "renderedfeatures.h"
#include "texturedscene.h"

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/feature/geometry.h>
#include <lain/camera/feature/matching.h>
#include <lain/camera/feature/sequenceextraction.h>
#include <lain/camera/projection.h>
#include <lain/core/time.h>
#include <lain/media/framesequence.h>
#include <lain/task/task.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
namespace synthetic = lain::camera::synthetic;
using camera::testing::ensureBackend;
using camera::testing::largeCamera;
using camera::testing::strongest;
using synthetic::projectInto;

namespace
{
	// How far `lens` moves the outer corner of its image from where a pinhole of the same intrinsics
	// puts that corner's ray, in pixels: what a fundamental matrix, which knows no lens, has to absorb
	// there.
	double cornerShift(const CameraModel& lens)
	{
		CameraModelParameters p;
		p.image = lens.image();
		p.intrinsics = lens.intrinsics();
		p.distortion = NoDistortion{};
		const ModelResult pinhole = CameraModel::create(p);
		REQUIRE(pinhole.model.has_value());
		const Unprojection<double> ray = unproject(lens, -0.5, -0.5);
		REQUIRE(ray.ok());
		const Projection<double> moved = project(*pinhole.model, ray.x, ray.y, ray.z);
		REQUIRE(moved.ok());
		return std::hypot(moved.u + 0.5, moved.v + 0.5);
	}

	double halfDiagonal(const CameraModel& m)
	{
		return 0.5 * std::hypot(double(m.image().width), double(m.image().height));
	}

	// Every corner of the default scene's box.
	std::array<math::Vec3d, 8> boxCorners()
	{
		std::array<math::Vec3d, 8> out;
		for (std::size_t i = 0; i < 8; ++i)
			out[i] = {i & 1 ? 0.2 : -0.6, i & 2 ? 0.8 : -0.1, i & 4 ? 0.4 : -0.2};
		return out;
	}
} // namespace

TEST_CASE("a moving camera's tracks are the same on the pool and agree with the truth", "[camera][opencv][feature]")
{
	ensureBackend();
	synthetic::Scene scene = synthetic::defaultScene();
	scene.noise = 1.0;
	// A textured disc crossing in front of the box, 0.4 m a frame: moving content, which a fundamental
	// matrix lets through only where it moves along the pair's epipolar lines.
	scene.occluder = synthetic::Occluder{{-1.6, 0.0, -1.0}, {0.4, 0.0, 0.0}, 0.35, {77, 0.03}};
	const CameraModel m = camera::testing::cameraWith(camera::testing::brownConrady());
	const std::size_t frames = 12;
	const std::vector<math::RigidTransformd> path = synthetic::arcPath(frames);
	const auto source = std::make_shared<synthetic::RenderedSource>("moving", scene, m, path, 1);
	const capture::CameraFootage footage{capture::CameraIdentity{"moving"}, media::FrameSequence::over(source)};

	// The source draws frame f at pose f.
	const image::Image last = footage.footage.image(frames - 1);
	const image::Image expected = synthetic::render(scene, m, path.back(), frames - 1, 2, 1);
	REQUIRE(last.width() == expected.width());
	REQUIRE(last.height() == expected.height());
	CHECK(std::equal(last.data(), last.data() + std::size_t(last.width()) * std::size_t(last.height()), expected.data()));

	// The box is in view in every frame, so every pair of frames overlaps.
	for (std::size_t f = 0; f < frames; ++f)
	{
		CAPTURE(f);
		for (const math::Vec3d& corner : boxCorners())
		{
			const std::optional<math::Vec2d> p = projectInto(m, path[f], corner);
			REQUIRE(p);
			CHECK(p->x >= 0);
			CHECK(p->y >= 0);
			CHECK(p->x <= double(m.image().width) - 1);
			CHECK(p->y <= double(m.image().height) - 1);
		}
	}

	SequenceExtractionRequest serial;
	serial.samples = 8;
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const SequenceExtractionResult reference = extractSequenceTracks(footage, serial);
	REQUIRE(reference.ok());
	SequenceExtractionResult pooled;
	{
		const lain::testing::ThreadPool pool{4};
		SequenceExtractionRequest request;
		request.samples = 8;
		pooled = extractSequenceTracks(footage, request);
	}
	REQUIRE(pooled.ok());
	REQUIRE(pooled.trackSet.tracks.size() == reference.trackSet.tracks.size());
	for (std::size_t t = 0; t < reference.trackSet.tracks.size(); ++t)
	{
		CHECK(pooled.trackSet.tracks[t].identity == reference.trackSet.tracks[t].identity);
		REQUIRE(pooled.trackSet.tracks[t].observations.size() == reference.trackSet.tracks[t].observations.size());
		for (std::size_t o = 0; o < reference.trackSet.tracks[t].observations.size(); ++o)
			CHECK(pooled.trackSet.tracks[t].observations[o].pixel == reference.trackSet.tracks[t].observations[o].pixel);
	}

	const SequenceExtractionReport& report = reference.report;
	CHECK(report.extractor.backend == "opencv");
	CHECK(report.matcher.backend == "opencv");
	CHECK(report.geometry.backend == "opencv");
	// A 640 x 480 frame is searched natively: LongestSide never enlarges. Measured: 778 to 980
	// keypoints a frame, 568 to 676 features once their orientations are kept together.
	REQUIRE(report.frames.size() == 8);
	for (const FrameExtraction& f : report.frames)
		CHECK(f.scale == 1.0);

	const std::vector<View>& views = reference.trackSet.views;
	REQUIRE(views.size() == 8);
	const auto ordinalOf = [&views](std::uint32_t view)
	{ return views[view].frames.front().ordinal; };
	// Measured: frames 0, 2, 3, 5, 6, 8, 9 and 11 sampled, and all 28 pairs verify, with 62 inliers
	// (frames 0 and 9) to 390 (frames 8 and 9). Adjacent ones are what must.
	for (const PairExtraction& p : report.pairs)
	{
		if (p.b == p.a + 1)
			CHECK(p.outcome == PairOutcome::Verified);
	}

	// The disc crosses the view in the sampled frames.
	std::size_t discSeen = 0;
	for (const View& v : views)
	{
		const std::size_t f = v.frames.front().ordinal;
		discSeen += projectInto(m, path[f], scene.occluder->centre(f)) ? 1 : 0;
	}
	CHECK(discSeen >= 2);

	// Each track's first observation, cast into the scene at its own frame. Those on the disc are
	// moving content a matrix let through; the rest are projected into every other member's frame.
	// Measured: 1028 tracks, 5 components rejected as conflicts (34 features). 11 tracks are the
	// disc's. Of the other 1017, 912 land within 2 px of every member and 88 more within 10 px, large
	// features in long tracks (the rig case's localisation); 17 are wrong, one member 56 to 571 px out
	// along its epipolar line, mostly on the floor, whose texture repeats along a grazing view. A
	// fundamental matrix cannot see a wrong match on the epipolar line, which is the evidence
	// sub-slice 9's robust loss and outlier report meet.
	std::size_t onDisc = 0, still = 0, consistent = 0, wrong = 0;
	for (const Track& t : reference.trackSet.tracks)
	{
		const SceneObservation& first = t.observations.front();
		const std::size_t f = ordinalOf(first.view);
		const std::optional<synthetic::Hit> hit = synthetic::truthAt(scene, m, path[f], first.pixel, f);
		REQUIRE(hit);
		if (hit->surface == synthetic::kOccluderSurface)
		{
			++onDisc;
			continue;
		}
		++still;
		double worst = 0;
		for (std::size_t o = 1; o < t.observations.size(); ++o)
		{
			const SceneObservation& other = t.observations[o];
			const std::optional<math::Vec2d> p = projectInto(m, path[ordinalOf(other.view)], hit->point);
			worst = std::max(worst, p ? math::length(*p - other.pixel) : 1e9);
		}
		consistent += worst < 2.0 ? 1 : 0;
		wrong += worst > 10.0 ? 1 : 0;
	}
	CAPTURE(reference.trackSet.tracks.size(), onDisc, still, consistent, wrong);
	CHECK(reference.trackSet.tracks.size() > 800);
	CHECK(onDisc <= 20);
	CHECK(double(consistent) / double(still) > 0.85);
	CHECK(double(wrong) / double(still) < 0.03);
}

TEST_CASE("what a fundamental matrix keeps through four lenses", "[camera][opencv][feature][scale]")
{
#ifndef NDEBUG
	SKIP("runs in Release builds: it draws and searches 16 frames of 1920 x 1080");
#endif
	ensureBackend();
	synthetic::Scene scene = synthetic::defaultScene();
	scene.noise = 1.0;
	// A disc in view in all four frames used, 4 cm a frame: about 33 px between adjacent frames at its
	// 1.8 m.
	scene.occluder = synthetic::Occluder{{-1.0, 0.0, -1.0}, {0.04, 0.0, 0.0}, 0.35, {77, 0.03}};
	const std::vector<math::RigidTransformd> path = synthetic::arcPath(40);

	// The test camera's field of view at 1920 x 1080 (f 1500), so a 1920 search is native, through
	// four lenses stated by how far each moves the image's corner.
	const std::array<double, 4> k1s{0.0, -0.1, -0.2, -0.25};
	const std::array<double, 4> shifts{0.0, 71.8, 192.6, 317.3}; // px: 0, 6.5, 17.5 and 28.8% of the half-diagonal
	std::vector<CameraModel> lenses;
	for (std::size_t l = 0; l < k1s.size(); ++l)
	{
		lenses.push_back(largeCamera(1920, 1080, BrownConrady5{k1s[l], 0.0, 0.0, 0.0, 0.0}));
		CHECK(std::abs(cornerShift(lenses.back()) - shifts[l]) < 0.1);
	}
	CHECK(std::abs(shifts[3] / halfDiagonal(lenses[3]) - 0.288) < 0.001);

	// A near pair (adjacent frames) and a far pair (the path's ends, 0.7 rad apart).
	const std::array<std::size_t, 4> used{0, 19, 20, 39};
	const std::array<std::array<std::size_t, 2>, 2> pairs{{{1, 2}, {0, 3}}}; // into `used`
	const std::array<double, 5> thresholds{1, 2, 4, 8, 16};

	// Every frame drawn and searched on the pool, then capped as extraction caps it.
	std::vector<Features> features(lenses.size() * used.size());
	{
		const lain::testing::ThreadPool pool{4};
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(features.size()),
					[&](std::ptrdiff_t i)
					{
						const std::size_t l = std::size_t(i) / used.size(), f = used[std::size_t(i) % used.size()];
						const ExtractResult found =
							extract(synthetic::render(scene, lenses[l], path[f], f, 2, f + 1), LongestSide{1920});
						if (found.ok())
							features[std::size_t(i)] = strongest(found.features, 8192);
					});
	}

	// What `fundamental` keeps of one pair's matches at one threshold: the worst of seeds 0 to 4.
	struct Kept
	{
		std::size_t correct = 0, outer = 0; // of the correct matches, and of those in the outer third
		std::size_t wrong = 0, disc = 0;	// of the wrong matches admitted, and of those the disc's
	};
	// [lens][pair][threshold], and what each pair's matching found to keep.
	std::array<std::array<std::array<Kept, 5>, 2>, 4> kept{};
	std::array<std::array<Kept, 2>, 4> found{};

	for (std::size_t l = 0; l < lenses.size(); ++l)
	{
		const CameraModel& lens = lenses[l];
		const math::Vec2d centre{lens.intrinsics().cx, lens.intrinsics().cy};
		for (std::size_t q = 0; q < pairs.size(); ++q)
		{
			CAPTURE(k1s[l], q);
			const std::size_t fa = used[pairs[q][0]], fb = used[pairs[q][1]];
			const Features& a = features[l * used.size() + pairs[q][0]];
			const Features& b = features[l * used.size() + pairs[q][1]];
			// Measured: 948 features or more a frame through every lens.
			REQUIRE(a.size() > 500);
			REQUIRE(b.size() > 500);
			const MatchResult matched = match(a, b);
			REQUIRE(matched.ok());
			// Each match judged against the truth, as twoViews judges one, and placed by its outer end.
			std::vector<PixelPair> all;
			std::vector<bool> correct, outer, disc;
			for (const Match& mt : matched.matches)
			{
				const math::Vec2d pa = a.keypoints[mt.a].pixel, pb = b.keypoints[mt.b].pixel;
				all.push_back({pa, pb});
				const std::optional<synthetic::Hit> hit = synthetic::truthAt(scene, lens, path[fa], pa, fa);
				const std::optional<math::Vec2d> p = hit ? projectInto(lens, path[fb], hit->point) : std::nullopt;
				correct.push_back(p && math::length(*p - pb) < 3.0);
				disc.push_back(hit && hit->surface == synthetic::kOccluderSurface);
				const double radius = std::max(math::length(pa - centre), math::length(pb - centre)) / halfDiagonal(lens);
				outer.push_back(radius > 2.0 / 3.0);
				found[l][q].correct += correct.back() ? 1 : 0;
				found[l][q].outer += correct.back() && outer.back() ? 1 : 0;
			}
			for (std::size_t t = 0; t < thresholds.size(); ++t)
			{
				Kept worst{found[l][q].correct, found[l][q].outer, 0, 0};
				for (std::uint64_t seed = 0; seed < 5; ++seed)
				{
					FundamentalRequest request;
					request.threshold = thresholds[t];
					request.seed = seed;
					const FundamentalResult result = fundamental(all, request);
					REQUIRE(result.ok());
					Kept k;
					for (const std::uint32_t i : result.inliers)
					{
						k.correct += correct[i] ? 1 : 0;
						k.outer += correct[i] && outer[i] ? 1 : 0;
						k.wrong += correct[i] ? 0 : 1;
						k.disc += !correct[i] && disc[i] ? 1 : 0;
					}
					worst = {std::min(worst.correct, k.correct), std::min(worst.outer, k.outer), std::max(worst.wrong, k.wrong),
							 std::max(worst.disc, k.disc)};
				}
				kept[l][q][t] = worst;
			}
		}
	}

	const auto share = [](std::size_t part, std::size_t whole)
	{ return whole == 0 ? 1.0 : double(part) / double(whole); };
	constexpr std::size_t near = 0, far = 1, at4 = 2, at16 = 4;

	// The near pair (frames 19 and 20), 448 to 580 correct matches, 141 to 223 in the outer third.
	// Measured, the worst seed: at 4 px every lens keeps every correct match, admitting 17 to 21 wrong
	// ones, the disc's at most 1. At 1 px each keeps 95 to 98% (the -0.25 lens 91.5% of its outer
	// third), and at 2 px 99 to 100%. Above 4 px a matrix takes the disc: at 16 px every lens admits
	// 20 to 22 of its 20 to 25 matches, and at 8 px the pinhole and the -0.2 lens lose up to 4% of their
	// correct matches to a matrix that explains the disc instead. Between adjacent frames a lens's
	// warp barely changes, so a matrix absorbs it.
	for (std::size_t l = 0; l < lenses.size(); ++l)
	{
		CAPTURE(k1s[l]);
		const Kept& k = kept[l][near][at4];
		CHECK(share(k.correct, found[l][near].correct) > 0.99);
		CHECK(share(k.outer, found[l][near].outer) > 0.99);
		CHECK(k.disc <= 2);
		CHECK(kept[l][near][at16].disc >= 15);
	}

	// The far pair (frames 0 and 39), 27 to 75 correct matches. The disc moves along the baseline, so
	// every threshold from 2 px admits 11 to 18 of its matches. Measured, the worst seed, kept at 4 px:
	//
	//   k1      corner shift   all              outer third
	//    0        0 px         27/27  100%      20/20  100%
	//   -0.1     72 px         45/45  100%      28/28  100%
	//   -0.2    193 px         71/75   94.7%    58/61   95.1%
	//   -0.25   317 px         66/75   88.0%    47/56   83.9%
	//
	// At 16 px the -0.25 lens keeps 97.3% (all of its outer third); at 1 px every lens keeps 68 to 89%.
	// Distortion costs what a wide baseline sees through it, peripheral matches first.
	const std::array<double, 4> outerAtLeast{0.99, 0.99, 0.9, 0.75};
	for (std::size_t l = 0; l < lenses.size(); ++l)
	{
		CAPTURE(k1s[l]);
		CHECK(share(kept[l][far][at4].outer, found[l][far].outer) > outerAtLeast[l]);
		CHECK(kept[l][far][at4].disc >= 5);
	}
	CHECK(share(kept[0][far][at4].correct, found[0][far].correct) > 0.99);
	CHECK(share(kept[3][far][at4].outer, found[3][far].outer) < 0.95);
}

TEST_CASE("the time to match every pair of 40 frames", "[camera][opencv][feature][scale]")
{
#ifndef NDEBUG
	SKIP("runs in Release builds, for its timings");
#endif
	ensureBackend();
	// The 1920 x 1080 camera along the path over the fine texture, so the cap binds, every frame drawn
	// on the pool first so nothing below times a render.
	const CameraModel m = largeCamera(1920, 1080);
	synthetic::Scene scene = synthetic::defaultScene(1, 0.01);
	scene.noise = 1.0;
	const auto source = std::make_shared<synthetic::RenderedSource>("fine", scene, m, synthetic::arcPath(40), 1);
	{
		const lain::testing::ThreadPool pool{4};
		source->drawAll();
	}
	const capture::CameraFootage footage{capture::CameraIdentity{"fine"}, media::FrameSequence::over(source)};

	// On one core: frames 0 and 1 capped at 8192 features with all their orientations, and the
	// pre-screen on the 512 strongest of those, both directions as match() asks.
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
		return (core::Time::now() - start).seconds();
	};
	const double screenExact = seconds(screenA, screenB, MatchRequest{});
	const double screenApproximate = seconds(screenA, screenB, approximate);
	const double fullExact = seconds(a, b, MatchRequest{});
	const double fullApproximate = seconds(a, b, approximate);

	// Every pair of all 40 frames at a cap of 1024, on the pool: the pre-screen reads the 512 strongest
	// of the capped features, so it skips the pairs the default cap would.
	SequenceExtractionRequest request;
	request.samples = 40;
	request.featureCap = 1024;
	SequenceExtractionResult result;
	{
		const lain::testing::ThreadPool pool{4};
		result = extractSequenceTracks(footage, request);
	}
	REQUIRE(result.ok());
	REQUIRE(result.report.pairs.size() == 780);
	std::size_t skipped = 0, verified = 0;
	for (const PairExtraction& p : result.report.pairs)
	{
		skipped += p.outcome == PairOutcome::Skipped ? 1 : 0;
		verified += p.outcome == PairOutcome::Verified ? 1 : 0;
	}

	// Measured on one core: 10756 and 10738 rows at the cap (1.31 a feature), 1070 and 1044 at the
	// pre-screen. The pre-screen took 0.039 s Exact and 0.033 s Approximate; the full match 5.59 s
	// Exact (6332 matches) and 0.41 s Approximate (6278). Brute force grows with the rows squared:
	// slice 3's pair of 12182 rows took 7.07 s on the same machine, which scales to 5.51 s here.
	//
	// For all 780 pairs of 40 frames, none skipped on this path, since every frame sees the box:
	//
	//                  without the pre-screen      with it
	//   Exact          1.21 core-hours (18 min)    1.22 core-hours (18 min)
	//   Approximate    5.3 core-minutes (80 s)     5.8 core-minutes (86 s)
	//
	// the wall times on four cores. The pre-screen pays for itself once more than 0.7% of pairs share
	// nothing under Exact, 8% under Approximate. The production run at a cap of 1024: 0 pairs skipped,
	// 780 verified, 2608 tracks, in 61 s on four workers.
	CAPTURE(screenExact, screenApproximate, fullExact, fullApproximate, skipped, verified, result.report.elapsed.seconds());
	CHECK(a.size() > 10000);
	CHECK(screenExact < 0.05 * fullExact);
	CHECK(screenApproximate < 0.3 * fullApproximate);
	CHECK(fullApproximate * 4.0 < fullExact);
	CHECK(skipped == 0);
	CHECK(verified == 780);
}
