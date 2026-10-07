// The OpenCV feature producer (SIFT, the matcher, the geometry solver) through lain's production
// facades, on textured scenes drawn in plain C++ (texturedscene.h), so every correspondence and pose
// is judged against a known truth. Every bound is a measurement, written beside its check; the cases
// marked Release only also record what slice 3 left provisional (WORK.md, M9 slice 3, sub-slice 7).

#include "testcamera.h"
#include "texturedscene.h"

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/feature/extraction.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/feature/geometry.h>
#include <lain/camera/feature/matching.h>
#include <lain/camera/opencv/register.h>
#include <lain/camera/projection.h>
#include <lain/core/time.h>
#include <lain/media/framesequence.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#if defined(__GLIBC__)
#	include <malloc.h>
#endif

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
namespace synthetic = lain::camera::synthetic;
using Catch::Matchers::ContainsSubstring;

namespace
{
	constexpr double kTau = 6.283185307179586;

	void ensureBackend()
	{
		static const bool once = []
		{
			opencv::registerBackend();
			return true;
		}();
		(void)once;
	}

	// A camera at `position` looking at `target`, with the reference frame's +Y as down.
	math::RigidTransformd lookingAt(const math::Vec3d& position, const math::Vec3d& target)
	{
		const math::Vec3d z = math::normalize(target - position);
		const math::Vec3d x = math::normalize(math::cross(math::Vec3d{0, 1, 0}, z));
		const math::Vec3d y = math::cross(z, x);
		return math::RigidTransformd{math::quat_cast(math::Mat3d{x, y, z}), position};
	}

	// A camera on an arc 3.2 m from the middle of the default scene, `angle` radians round it.
	math::RigidTransformd arcCamera(double angle)
	{
		const math::Vec3d target{-0.2, 0.2, 0.4};
		return lookingAt(target + math::Vec3d{3.2 * std::sin(angle), -0.5, -3.2 * std::cos(angle)}, target);
	}

	CameraModel model()
	{
		return camera::testing::cameraWith(camera::testing::brownConrady());
	}

	// A pinhole of `width` x `height` with the test camera's field of view, for the large-image cases.
	CameraModel pinhole(std::uint32_t width, std::uint32_t height)
	{
		CameraModelParameters p = camera::testing::parametersWith(NoDistortion{});
		const double f = 500.0 * double(width) / 640.0;
		p.image = {width, height};
		p.intrinsics = {f, f, 0.5 * double(width) - 0.5, 0.5 * double(height) - 0.5};
		ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		return 2.0 * std::acos(std::min(1.0, std::abs(math::dot(a.rotation(), b.rotation()))));
	}

	double angleBetween(const math::Vec3d& u, const math::Vec3d& v)
	{
		return std::atan2(math::length(math::cross(u, v)), math::dot(u, v));
	}

	math::Vec3d rayOf(const CameraModel& m, const math::Vec2d& pixel)
	{
		const Unprojection<double> r = unproject(m, pixel.x, pixel.y);
		return r.ok() ? math::Vec3d{r.x, r.y, r.z} : math::Vec3d{0.0};
	}

	// Where the camera at `referenceFromCamera` sees `point`, or nothing.
	std::optional<math::Vec2d> projectInto(const CameraModel& m, const math::RigidTransformd& referenceFromCamera,
										   const math::Vec3d& point)
	{
		const math::Vec3d p = referenceFromCamera.inverse().apply(point);
		if (p.z <= 0)
			return std::nullopt;
		const Projection<double> px = project(m, p.x, p.y, p.z);
		if (!px.ok())
			return std::nullopt;
		return math::Vec2d{px.u, px.v};
	}

	double wrapped(double a)
	{
		const double t = std::fmod(a, kTau);
		return t < 0 ? t + kTau : t;
	}

	std::pair<long long, long long> cellOf(const Keypoint& k)
	{
		return {std::llround(k.pixel.x * 64), std::llround(k.pixel.y * 64)};
	}

	std::map<std::pair<long long, long long>, std::vector<std::uint32_t>> cells(const Features& f)
	{
		std::map<std::pair<long long, long long>, std::vector<std::uint32_t>> out;
		for (std::uint32_t i = 0; i < f.size(); ++i)
			out[cellOf(f.keypoints[i])].push_back(i);
		return out;
	}

	// The rule extractTracks used until 2026-10-07, before matching: one keypoint per 1/64-pixel cell,
	// the largest response, then the smallest angle in [0, 2pi), then the smallest size. Restated here
	// to measure what it cost, which is why it was replaced.
	std::uint32_t kept(const Features& f, const std::vector<std::uint32_t>& members)
	{
		return *std::min_element(members.begin(), members.end(),
								 [&](std::uint32_t x, std::uint32_t y)
								 {
									 const Keypoint& a = f.keypoints[x];
									 const Keypoint& b = f.keypoints[y];
									 return std::make_tuple(-a.response, wrapped(a.angle), a.size) <
											std::make_tuple(-b.response, wrapped(b.angle), b.size);
								 });
	}

	Features subset(const Features& f, const std::vector<std::uint32_t>& keep)
	{
		Features out = f;
		out.keypoints.clear();
		out.descriptors.clear();
		for (const std::uint32_t i : keep)
		{
			out.keypoints.push_back(f.keypoints[i]);
			out.descriptors.insert(out.descriptors.end(), f.descriptor(i), f.descriptor(i) + f.descriptorBytes);
		}
		return out;
	}

	// The `n` strongest features, each with all its orientations, as extractTracks caps a frame.
	Features strongest(const Features& f, std::size_t n)
	{
		std::vector<std::vector<std::uint32_t>> features;
		for (const auto& [cell, members] : cells(f))
			features.push_back(members);
		const auto response = [&f](const std::vector<std::uint32_t>& rows)
		{
			double best = 0;
			for (const std::uint32_t r : rows)
				best = std::max(best, f.keypoints[r].response);
			return best;
		};
		std::stable_sort(features.begin(), features.end(), [&](const auto& a, const auto& b)
						 { return response(a) > response(b); });
		features.resize(std::min(n, features.size()));
		std::vector<std::uint32_t> rows;
		for (const std::vector<std::uint32_t>& feature : features)
			rows.insert(rows.end(), feature.begin(), feature.end());
		return subset(f, rows);
	}

	// Two views of the default scene and their matched features, each match judged against the truth.
	struct TwoViews
	{
		synthetic::Scene scene = synthetic::defaultScene();
		CameraModel camera = model();
		math::RigidTransformd a = arcCamera(-0.15);
		math::RigidTransformd b = arcCamera(0.15);
		Features fa, fb;
		MatchResult matches;
		std::vector<bool> correct;		// per match: A's truth point lands within 3 px of B's feature
		std::vector<double> residual;	// per match, px
		std::vector<math::Vec3d> truth; // per match: what A's pixel sees
	};

	TwoViews twoViews(const ScalePolicy& scale = NativeScale{})
	{
		TwoViews v;
		v.scene.noise = 1.0;
		const ExtractResult ea = extract(synthetic::render(v.scene, v.camera, v.a, 0, 2, 1), scale);
		const ExtractResult eb = extract(synthetic::render(v.scene, v.camera, v.b, 0, 2, 2), scale);
		REQUIRE(ea.ok());
		REQUIRE(eb.ok());
		v.fa = ea.features;
		v.fb = eb.features;
		v.matches = match(v.fa, v.fb);
		REQUIRE(v.matches.ok());
		for (const Match& m : v.matches.matches)
		{
			const math::Vec2d pa = v.fa.keypoints[m.a].pixel;
			const math::Vec2d pb = v.fb.keypoints[m.b].pixel;
			const std::optional<synthetic::Hit> hit = synthetic::truthAt(v.scene, v.camera, v.a, pa);
			const std::optional<math::Vec2d> predicted = hit ? projectInto(v.camera, v.b, hit->point) : std::nullopt;
			const double r = predicted ? math::length(*predicted - pb) : 1e9;
			v.residual.push_back(r);
			v.correct.push_back(r < 3.0);
			v.truth.push_back(hit ? hit->point : math::Vec3d{0.0});
		}
		return v;
	}

	// Of the matches judged correct, the distinct cell pairs they join.
	std::set<std::pair<std::pair<long long, long long>, std::pair<long long, long long>>>
	correctCells(const Features& fa, const Features& fb, const MatchResult& m, const TwoViews& v)
	{
		std::set<std::pair<std::pair<long long, long long>, std::pair<long long, long long>>> out;
		for (const Match& mt : m.matches)
		{
			const std::optional<synthetic::Hit> hit = synthetic::truthAt(v.scene, v.camera, v.a, fa.keypoints[mt.a].pixel);
			const std::optional<math::Vec2d> p = hit ? projectInto(v.camera, v.b, hit->point) : std::nullopt;
			if (p && math::length(*p - fb.keypoints[mt.b].pixel) < 3.0)
				out.insert({cellOf(fa.keypoints[mt.a]), cellOf(fb.keypoints[mt.b])});
		}
		return out;
	}

#if defined(__GLIBC__)
	// This process's resident set, and its peak since the last resetPeak(), in bytes.
	double statusBytes(const std::string& field)
	{
		std::ifstream in("/proc/self/status");
		std::string line;
		while (std::getline(in, line))
			if (line.rfind(field + ":", 0) == 0)
				return std::stod(line.substr(field.size() + 1)) * 1024.0;
		return 0;
	}

	// Hands freed heap memory back first, or what the allocator kept resident would be reused
	// without raising the peak.
	bool resetPeak()
	{
		malloc_trim(0);
		std::ofstream out("/proc/self/clear_refs");
		out << "5";
		return bool(out);
	}
#endif
} // namespace

TEST_CASE("a feature searched at half scale lands where the native search finds it", "[camera][opencv][feature]")
{
	ensureBackend();
	const synthetic::Scene scene = synthetic::defaultScene();
	const CameraModel m = model();
	const image::Image frame = synthetic::render(scene, m, arcCamera(0.0));
	const ExtractResult native = extract(frame, NativeScale{});
	const ExtractResult half = extract(frame, ScaleFactor{0.5});
	REQUIRE(native.ok());
	REQUIRE(half.ok());
	CHECK(half.features.scale == 0.5);
	CHECK(native.features.kind == "sift");
	CHECK(native.features.descriptorBytes == 128);

	// The same feature found at both scales sits at one source pixel, on average: centre-to-centre
	// mapping leaves no offset. Measured: 484 of 773 half-scale features matched within 2 px, mean
	// offset (-0.021, 0.002) px. Mapping corner to corner instead shifts it by 0.5 px on each axis.
	const MatchResult matched = match(half.features, native.features);
	REQUIRE(matched.ok());
	math::Vec2d sum{0.0};
	std::size_t n = 0;
	for (const Match& mt : matched.matches)
	{
		const math::Vec2d d = half.features.keypoints[mt.a].pixel - native.features.keypoints[mt.b].pixel;
		if (math::length(d) > 2.0)
			continue;
		sum += d;
		++n;
	}
	REQUIRE(n > 300);
	CHECK(std::abs(sum.x / double(n)) < 0.1);
	CHECK(std::abs(sum.y / double(n)) < 0.1);
}

TEST_CASE("matched features correspond in the scene", "[camera][opencv][feature]")
{
	ensureBackend();
	const TwoViews v = twoViews();
	// Measured: 1036 and 1020 features, 324 matches, 317 of them within 3 px of the truth, RMS 0.438 px.
	REQUIRE(v.matches.matches.size() > 250);
	std::size_t correct = 0;
	double sum = 0, sumBySize = 0;
	std::map<int, std::pair<double, std::size_t>> bySize; // keypoint size band -> (sum of squares, count)
	for (std::size_t i = 0; i < v.correct.size(); ++i)
	{
		if (!v.correct[i])
			continue;
		++correct;
		const Match& m = v.matches.matches[i];
		const double size = 0.5 * (v.fa.keypoints[m.a].size + v.fb.keypoints[m.b].size);
		sum += v.residual[i] * v.residual[i];
		sumBySize += (v.residual[i] / size) * (v.residual[i] / size);
		auto& band = bySize[size < 4 ? 0 : size < 16 ? 1
													 : 2];
		band.first += v.residual[i] * v.residual[i];
		++band.second;
	}
	CHECK(double(correct) / double(v.matches.matches.size()) > 0.95);
	CHECK(std::sqrt(sum / double(correct)) < 0.6);

	// SIFT's localisation scales with the feature: the residual, to which two keypoints' errors add,
	// is 4.9% of their mean size, RMS, so 3.4% per keypoint. By size band, measured RMS 0.17 px below
	// 4 px and 0.98 px at 16 px and above. So a covariance follows the size
	// (ExtractionRequest::localisationPerSize); the fixed 0.7 px it replaced was three times too loose
	// for the many small features and about right only for the few largest.
	const auto rms = [&](int band)
	{ return std::sqrt(bySize[band].first / double(bySize[band].second)); };
	CHECK(std::sqrt(sumBySize / double(correct)) < 0.07);
	REQUIRE(bySize[0].second > 20);
	REQUIRE(bySize[2].second > 10);
	CHECK(rms(2) > 3.0 * rms(0));

	// The proportion holds at a coarser search, which is what lets ExtractionRequest::
	// localisationPerSize state a covariance from the size alone: a feature found at half scale is
	// larger in source pixels, and its error grows with it. Measured at ScaleFactor{0.5}: 235 correct
	// of 236 matches, 6.3% of the size against 4.9% natively. The excess sits in the smallest
	// features (6.7% below 8 source px, 4.6% at 16 to 32), and stays inside the native bound.
	const TwoViews half = twoViews(ScaleFactor{0.5});
	double halfBySize = 0;
	std::size_t halfCorrect = 0;
	for (std::size_t i = 0; i < half.correct.size(); ++i)
	{
		if (!half.correct[i])
			continue;
		const Match& m = half.matches.matches[i];
		const double size = 0.5 * (half.fa.keypoints[m.a].size + half.fb.keypoints[m.b].size);
		halfBySize += (half.residual[i] / size) * (half.residual[i] / size);
		++halfCorrect;
	}
	REQUIRE(halfCorrect > 150);
	CHECK(std::sqrt(halfBySize / double(halfCorrect)) < 0.07);
}

TEST_CASE("the relative and absolute poses of two views are recovered", "[camera][opencv][feature]")
{
	ensureBackend();
	const TwoViews v = twoViews();
	const math::RigidTransformd truthBFromA = v.b.inverse() * v.a;
	const math::RigidTransformd truthCameraFromReference = v.b.inverse();
	std::vector<RayPair> all;
	std::vector<PointRay> pointRays;
	for (std::size_t i = 0; i < v.matches.matches.size(); ++i)
	{
		const Match& m = v.matches.matches[i];
		const RayPair pair{rayOf(v.camera, v.fa.keypoints[m.a].pixel), rayOf(v.camera, v.fb.keypoints[m.b].pixel)};
		all.push_back(pair);
		if (v.correct[i])
			pointRays.push_back({v.truth[i], pair.b});
	}
	// Every seed: the solver's sampling must not decide whether the pose is right. Measured over these
	// 20 seeds on all 324 matches: rotation 0.24 to 0.47 mrad, translation direction 0.12 to 1.14 mrad;
	// the absolute pose from the 317 truth points, 0.12 to 1.0 mrad and 0.24 to 1.69 mm. Fed OpenCV's
	// normalised coordinates (focal 1) instead of the virtual pinhole, MAGSAC++ drifts to 15 mrad.
	for (std::uint64_t seed = 0; seed < 20; ++seed)
	{
		CAPTURE(seed);
		GeometryRequest request;
		request.seed = seed;
		const GeometryResult relative = relativePose(all, request);
		REQUIRE(relative.ok());
		// All four decompositions of each essential matrix, so lain chooses among them.
		CHECK(relative.candidates > 0);
		CHECK(relative.candidates % 4 == 0);
		CHECK(rotationBetween(*relative.pose, truthBFromA) < 1e-3);
		CHECK(angleBetween(relative.pose->translation(), truthBFromA.translation()) < 2.5e-3);

		const GeometryResult absolute = absolutePose(pointRays, request);
		REQUIRE(absolute.ok());
		CHECK(absolute.candidates >= 1);
		CHECK(rotationBetween(*absolute.pose, truthCameraFromReference) < 2e-3);
		CHECK(math::length(absolute.pose->translation() - truthCameraFromReference.translation()) < 3e-3);
	}
}

TEST_CASE("collapsing orientations before matching loses a fifth of the correct matches", "[camera][opencv][feature]")
{
	ensureBackend();
	const TwoViews v = twoViews();
	const auto ca = cells(v.fa), cb = cells(v.fb);

	// SIFT gives a keypoint with two strong orientation peaks twice, at one pixel and one size.
	// Measured: 297 of 1036 keypoints share their cell with another.
	std::size_t duplicates = 0;
	for (const auto& [cell, members] : ca)
		duplicates += members.size() - 1;
	CHECK(double(duplicates) / double(v.fa.size()) > 0.1);

	// Duplicates share a response, so the tie-break falls to the smallest angle, which says nothing
	// about the other view. Of the correctly matched cells holding duplicates in both views, the kept
	// pair's orientations differ as the matched pair's do in 83 of 128.
	std::size_t both = 0, agree = 0;
	for (std::size_t i = 0; i < v.matches.matches.size(); ++i)
	{
		if (!v.correct[i])
			continue;
		const Match& m = v.matches.matches[i];
		const auto& ma = ca.at(cellOf(v.fa.keypoints[m.a]));
		const auto& mb = cb.at(cellOf(v.fb.keypoints[m.b]));
		if (ma.size() < 2 || mb.size() < 2)
			continue;
		++both;
		const double roll = wrapped(v.fa.keypoints[m.a].angle - v.fb.keypoints[m.b].angle);
		const double keptRoll = wrapped(v.fa.keypoints[kept(v.fa, ma)].angle - v.fb.keypoints[kept(v.fb, mb)].angle);
		const double d = std::abs(roll - keptRoll);
		agree += std::min(d, kTau - d) < 0.3 ? 1 : 0;
	}
	REQUIRE(both > 50);
	CHECK(double(agree) / double(both) > 0.5);

	// What that costs: of the cell pairs a match joins correctly, 259 before collapsing and 205 after.
	// A fifth of the correct matches go, because a cell whose views kept disagreeing orientations
	// keeps two descriptors that no longer match. Which is why extraction keeps every orientation of
	// a feature and matches them all (since 2026-10-07).
	const auto keep = [](const Features& f, const auto& byCell)
	{
		std::vector<std::uint32_t> out;
		for (const auto& [cell, members] : byCell)
			out.push_back(kept(f, members));
		return subset(f, out);
	};
	const Features ka = keep(v.fa, ca), kb = keep(v.fb, cb);
	const MatchResult collapsed = match(ka, kb);
	REQUIRE(collapsed.ok());
	const std::size_t before = correctCells(v.fa, v.fb, v.matches, v).size();
	const std::size_t after = correctCells(ka, kb, collapsed, v).size();
	CAPTURE(before, after);
	CHECK(after < before);
	CHECK(double(after) > 0.7 * double(before));
}

TEST_CASE("rendered footage through extractTracks is the same on the pool and agrees with the truth",
		  "[camera][opencv][feature]")
{
	ensureBackend();
	synthetic::Scene scene = synthetic::defaultScene();
	scene.noise = 1.0;
	// A textured disc crossing in front of the box, 0.4 m a frame: the moving content to drop.
	scene.occluder = synthetic::Occluder{{-1.6, 0.0, -1.0}, {0.4, 0.0, 0.0}, 0.35, {77, 0.03}};
	const CameraModel m = model();
	const std::vector<double> angles{-0.25, 0.0, 0.25};
	const std::size_t frames = 9;
	std::vector<RigFootage> cameras;
	std::vector<capture::CameraFootage> byCamera;
	for (std::size_t c = 0; c < angles.size(); ++c)
	{
		const std::string name = "cam0" + std::to_string(c);
		cameras.push_back({capture::CameraIdentity{name}, m,
						   media::FrameSequence::over(std::make_shared<synthetic::RenderedSource>(
							   name, scene, m, arcCamera(angles[c]), frames, std::uint64_t(c + 1)))});
		byCamera.push_back({cameras.back().camera, cameras.back().footage});
	}
	const std::vector<capture::CaptureGroup> groups = capture::groupsByPosition(byCamera).groups;

	ExtractionRequest serial;
	serial.execution = ExecutionPolicy::DeterministicDebug;
	const ExtractionResult reference = extractTracks(cameras, groups, serial);
	REQUIRE(reference.ok());
	ExtractionResult parallel, approximate;
	{
		const lain::testing::ThreadPool pool{4};
		parallel = extractTracks(cameras, groups, ExtractionRequest{});
		// Approximate search builds FLANN indexes on several workers at once.
		ExtractionRequest request;
		request.matching.search = MatchSearch::Approximate;
		approximate = extractTracks(cameras, groups, request);
	}
	REQUIRE(parallel.ok());
	REQUIRE(approximate.ok());
	REQUIRE(parallel.trackSet.tracks.size() == reference.trackSet.tracks.size());
	for (std::size_t t = 0; t < reference.trackSet.tracks.size(); ++t)
	{
		CHECK(parallel.trackSet.tracks[t].identity == reference.trackSet.tracks[t].identity);
		REQUIRE(parallel.trackSet.tracks[t].observations.size() == reference.trackSet.tracks[t].observations.size());
		for (std::size_t o = 0; o < reference.trackSet.tracks[t].observations.size(); ++o)
			CHECK(parallel.trackSet.tracks[t].observations[o].pixel == reference.trackSet.tracks[t].observations[o].pixel);
	}

	// Measured: every pair verified (288/286, 176/169 and 281/277 matches/inliers, after dropping 3, 2
	// and 2 features matched twice), 429 tracks, one conflict rejected, 1282 features dropped as
	// transient. Collapsing each feature to one orientation before matching, as extraction did
	// until 2026-10-07, gave 365 tracks from 225, 121 and 219 inliers.
	for (const PairExtraction& p : reference.report.pairs)
		CHECK(p.outcome == PairOutcome::Verified);
	CHECK(reference.trackSet.tracks.size() > 250);
	// Measured over seven runs: Approximate found 426 to 429 tracks, against Exact's 429. It differs
	// from run to run, which is why DeterministicDebug refuses it.
	CAPTURE(approximate.trackSet.tracks.size());
	CHECK(double(approximate.trackSet.tracks.size()) > 0.95 * double(reference.trackSet.tracks.size()));
	CHECK(double(approximate.trackSet.tracks.size()) < 1.05 * double(reference.trackSet.tracks.size()));
	CHECK(reference.report.conflicts <= 2);
	std::uint32_t transient = 0;
	for (const CameraExtraction& c : reference.report.cameras)
		transient += c.transient;
	CHECK(transient > 0);

	// The occluder crosses every camera's view during the sampled frames.
	std::vector<std::size_t> sampled;
	for (const media::FrameRef& f : reference.trackSet.views.front().frames)
		sampled.push_back(f.ordinal);
	for (std::size_t c = 0; c < angles.size(); ++c)
	{
		std::size_t seen = 0;
		for (const std::size_t f : sampled)
			seen += projectInto(m, arcCamera(angles[c]), scene.occluder->centre(f)) ? 1 : 0;
		CHECK(seen >= 2);
	}

	// Each track's first observation, cast into the scene without the occluder, lands on the others.
	// Measured: 410 of 429 tracks within 2 px everywhere, and the rest at most 6.7 px out, none of
	// them near a surface edge: large features, whose localisation scales with their size (the
	// correspondence case). A track of the occluder could not land anywhere near. And no observation
	// is on the occluder in most of the sampled frames, while 345 were hidden by it in at least one:
	// a static feature survives being covered for a frame or two.
	synthetic::Scene still = scene;
	still.occluder.reset();
	std::size_t consistent = 0, coveredOnce = 0;
	double worst = 0;
	for (const Track& t : reference.trackSet.tracks)
	{
		const SceneObservation& first = t.observations.front();
		const std::optional<synthetic::Hit> hit = synthetic::truthAt(still, m, arcCamera(angles[first.view]), first.pixel);
		REQUIRE(hit);
		bool all = true;
		for (std::size_t o = 1; o < t.observations.size(); ++o)
		{
			const SceneObservation& other = t.observations[o];
			const std::optional<math::Vec2d> p = projectInto(m, arcCamera(angles[other.view]), hit->point);
			const double r = p ? math::length(*p - other.pixel) : 1e9;
			worst = std::max(worst, r);
			all = all && r < 2.0;
		}
		consistent += all ? 1 : 0;
		for (const SceneObservation& o : t.observations)
		{
			std::size_t covered = 0;
			for (const std::size_t f : sampled)
			{
				const std::optional<synthetic::Hit> h = synthetic::truthAt(scene, m, arcCamera(angles[o.view]), o.pixel, f);
				covered += h && h->surface == -1 ? 1 : 0;
			}
			CHECK(covered * 2 < sampled.size());
			coveredOnce += covered > 0 ? 1 : 0;
		}
	}
	CHECK(double(consistent) / double(reference.trackSet.tracks.size()) > 0.9);
	CHECK(worst < 10.0);
	CHECK(coveredOnce > 0);
}

TEST_CASE("approximate search is refused under deterministic debugging, with the real matcher", "[camera][opencv][feature]")
{
	ensureBackend();
	const CameraModel m = model();
	std::vector<RigFootage> cameras;
	std::vector<capture::CameraFootage> byCamera;
	for (std::size_t c = 0; c < 2; ++c)
	{
		const std::string name = "cam0" + std::to_string(c);
		cameras.push_back({capture::CameraIdentity{name}, m,
						   media::FrameSequence::over(std::make_shared<synthetic::RenderedSource>(
							   name, synthetic::defaultScene(), m, arcCamera(0.2 * double(c)), 3))});
		byCamera.push_back({cameras.back().camera, cameras.back().footage});
	}
	ExtractionRequest request;
	request.matching.search = MatchSearch::Approximate;
	request.execution = ExecutionPolicy::DeterministicDebug;
	const ExtractionResult refused = extractTracks(cameras, capture::groupsByPosition(byCamera).groups, request);
	CHECK(refused.status == Status::Unsupported);
	CHECK_THAT(refused.detail, ContainsSubstring("reproducible only within a tolerance"));
}

TEST_CASE("an 8K frame is searched at the default size in bounded memory", "[camera][opencv][feature][scale]")
{
#ifndef NDEBUG
	SKIP("runs in Release builds: it renders an 8K frame and measures the search");
#endif
	ensureBackend();
	// Measured on four cores: the render takes 11 s; searching it at a longest side of 1600 px took
	// 0.54 s and found 1050 features, at 1920 px 0.60 s and 976. SIFT's first octave doubles the
	// searched image and keeps a float pyramid of it, so the search, not the frame, sets the memory:
	// peak growth 340 MB at 1600 px and 489 MB at 1920 px, per frame searched at once (glibc only,
	// from VmHWM), on top of the decoded frame (33 MB of 8K grey here, 100 MB of 8K RGB8).
	const CameraModel m = pinhole(7680, 4320);
	const image::Image frame = synthetic::render(synthetic::defaultScene(), m, arcCamera(0.0), 0, 1, 1);
	double previous = 0;
	for (const std::uint32_t side : {1600u, 1920u})
	{
		CAPTURE(side);
#if defined(__GLIBC__)
		const bool peaks = resetPeak();
		const double before = statusBytes("VmRSS");
#endif
		const ExtractResult found = extract(frame, LongestSide{side});
		REQUIRE(found.ok());
		CHECK(found.features.scale == double(side) / 7680.0);
		CHECK(found.features.size() > 500);
#if defined(__GLIBC__)
		if (peaks)
		{
			const double peak = statusBytes("VmHWM") - before;
			CAPTURE(peak);
			CHECK(peak < 1e9);
			CHECK(peak > previous);
			previous = peak;
		}
#endif
	}
	(void)previous;
}

TEST_CASE("exact and approximate search at 8192 features a view", "[camera][opencv][feature][scale]")
{
#ifndef NDEBUG
	SKIP("runs in Release builds, for its timings");
#endif
	ensureBackend();
	// Two 1920 x 1080 views 0.3 rad apart of a finely textured scene: 13238 and 12745 SIFT keypoints,
	// capped at the 8192 strongest features each with all their orientations, as extractTracks caps
	// a frame: 12182 and 12137 rows, 1.49 a feature. (Lowe reports about 15% of keypoints with a
	// second orientation; this texture's soft blobs give far more.) And a third frame of the first
	// view with other noise, which is what consolidating one camera's frames matches.
	const CameraModel m = pinhole(1920, 1080);
	synthetic::Scene scene = synthetic::defaultScene(1, 0.01);
	scene.noise = 1.0;
	const auto features = [&](double angle, std::uint64_t seed)
	{
		const ExtractResult found = extract(synthetic::render(scene, m, arcCamera(angle), 0, 2, seed), NativeScale{});
		REQUIRE(found.ok());
		return strongest(found.features, 8192);
	};
	const Features fa = features(-0.15, 1), fb = features(0.15, 2), again = features(-0.15, 3);
	REQUIRE(cells(fa).size() == 8192);
	REQUIRE(cells(fb).size() == 8192);

	// Measured on one core, both directions as match() asks: Exact 3683 matches in 10.2 s, Approximate
	// 3637 in 0.78 s with 96.8% of Exact's; consolidation (Exact) 10816 in 10.5 s. Brute force grows
	// with the rows, so keeping every orientation costs 2.8 times the 3.68 s of 8192 rows. For 100
	// cameras whose every pair overlaps, 4950 pair matches and 1000 consolidation matches: 16.9
	// core-hours Exact, 4.0 with Approximate for the pairs, 1.5 with Approximate throughout. Brute
	// force costs the same whatever the content, so the consolidation match costs a pair match.
	MatchRequest approximateRequest;
	approximateRequest.search = MatchSearch::Approximate;
	core::Time start = core::Time::now();
	const MatchResult exact = match(fa, fb);
	const double exactSeconds = (core::Time::now() - start).seconds();
	start = core::Time::now();
	const MatchResult approximate = match(fa, fb, approximateRequest);
	const double approximateSeconds = (core::Time::now() - start).seconds();
	const MatchResult consolidation = match(fa, again);
	REQUIRE(exact.ok());
	REQUIRE(approximate.ok());
	REQUIRE(consolidation.ok());
	CHECK(exact.matches.size() > 2000);
	CHECK(consolidation.matches.size() > exact.matches.size());

	std::set<std::pair<std::uint32_t, std::uint32_t>> found;
	for (const Match& mt : exact.matches)
		found.insert({mt.a, mt.b});
	std::size_t recalled = 0;
	for (const Match& mt : approximate.matches)
		recalled += found.count({mt.a, mt.b});
	CAPTURE(exactSeconds, approximateSeconds, recalled, exact.matches.size());
	CHECK(double(recalled) / double(exact.matches.size()) > 0.9);
	CHECK(approximateSeconds * 3.0 < exactSeconds);
}
