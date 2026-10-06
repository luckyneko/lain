// The feature facades (ADR-0016, "Producers propose; lain decides") over scripted stand-ins: the
// extractor's answer is checked rather than trusted, the ratio and mutual tests are lain's, and every
// candidate pose a backend proposes is measured by lain, whatever order it came in. Every tolerance is
// a measurement, written beside its check.

#include <lain/camera/feature/features.h>
#include <lain/camera/feature/geometry.h>
#include <lain/camera/feature/matching.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::feature;
using Catch::Matchers::ContainsSubstring;

namespace
{
	constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

	// --- the stand-ins ----------------------------------------------------------------

	struct ExtractorScript
	{
		std::optional<Features> answer;
		image::PixelFormat refuse = image::PixelFormat::Gray16;
		double askedScale = 0;
	};
	ExtractorScript& extractorScript()
	{
		static ExtractorScript instance;
		return instance;
	}

	class ScriptedExtractor : public Extractor
	{
	public:
		Provenance provenance() const override { return {"scripted", "1"}; }
		std::optional<Features> extract(const image::Image& image, double scale) const override
		{
			extractorScript().askedScale = scale;
			if (image.pixelFormat() == extractorScript().refuse)
				return std::nullopt;
			return extractorScript().answer;
		}
	};

	// Toy features: one byte of descriptor each, compared by absolute difference, so a case can say
	// exactly how far apart two features are.
	Features toy(const std::vector<int>& values, const std::string& kind = "toy")
	{
		Features f;
		f.kind = kind;
		f.descriptorBytes = 1;
		f.image = {1024, 16};
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			f.keypoints.push_back({{double(i), 0.0}, 1.0, 0.0, 1.0});
			f.descriptors.push_back(std::uint8_t(values[i]));
		}
		return f;
	}

	// Each feature of `a`'s two nearest in `b` by brute force, ties to the lower index.
	std::vector<std::array<Neighbour, 2>> bruteForce(const Features& a, const Features& b)
	{
		std::vector<std::array<Neighbour, 2>> out;
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			std::vector<Neighbour> all;
			for (std::size_t j = 0; j < b.size(); ++j)
				all.push_back({std::uint32_t(j), std::abs(double(*a.descriptor(i)) - double(*b.descriptor(j)))});
			std::stable_sort(all.begin(), all.end(), [](const Neighbour& x, const Neighbour& y)
							 { return x.distance < y.distance; });
			out.push_back({all[0], all[1]});
		}
		return out;
	}

	// Registered as "a-exact": Exact only, and the one matcher of "exactonly" features.
	class ExactMatcher : public Matcher
	{
	public:
		Provenance provenance() const override { return {"a-exact", "1"}; }
		bool accepts(std::string_view kind) const override { return kind == "toy" || kind == "exactonly"; }
		bool supports(MatchSearch search) const override { return search == MatchSearch::Exact; }
		std::vector<std::array<Neighbour, 2>> nearest(const Features& a, const Features& b, MatchSearch) const override
		{
			return bruteForce(a, b);
		}
	};

	// Registered as "b-both": both searches, of toy features only.
	class BothMatcher : public ExactMatcher
	{
	public:
		Provenance provenance() const override { return {"b-both", "1"}; }
		bool accepts(std::string_view kind) const override { return kind == "toy"; }
		bool supports(MatchSearch) const override { return true; }
	};

	// Registered as "c-broken", the one matcher of "broken" features: it answers as scripted.
	enum class Broken
	{
		WrongCount,
		OutOfRange,
		Repeated,
		Unordered,
	};
	Broken& brokenScript()
	{
		static Broken instance = Broken::WrongCount;
		return instance;
	}
	class BrokenMatcher : public Matcher
	{
	public:
		Provenance provenance() const override { return {"c-broken", "1"}; }
		bool accepts(std::string_view kind) const override { return kind == "broken"; }
		bool supports(MatchSearch) const override { return true; }
		std::vector<std::array<Neighbour, 2>> nearest(const Features& a, const Features& b, MatchSearch) const override
		{
			std::vector<std::array<Neighbour, 2>> out = bruteForce(a, b);
			switch (brokenScript())
			{
				case Broken::WrongCount:
					out.pop_back();
					break;
				case Broken::OutOfRange:
					out[0][1].index = std::uint32_t(b.size());
					break;
				case Broken::Repeated:
					out[0][1].index = out[0][0].index;
					break;
				case Broken::Unordered:
					std::swap(out[0][0], out[0][1]);
					out[0][1].distance = out[0][0].distance - 1;
					break;
			}
			return out;
		}
	};

	// What the geometry stand-in proposes, and what it was handed.
	struct GeometryScript
	{
		std::vector<math::RigidTransformd> candidates;
		std::size_t calls = 0;
		std::size_t handed = 0;
	};
	GeometryScript& geometryScript()
	{
		static GeometryScript instance;
		return instance;
	}
	void propose(std::vector<math::RigidTransformd> candidates)
	{
		geometryScript() = {};
		geometryScript().candidates = std::move(candidates);
	}

	class ScriptedGeometry : public GeometrySolver
	{
	public:
		Provenance provenance() const override { return {"scripted", "1"}; }
		std::vector<math::RigidTransformd> relativePoses(const std::vector<RayPair>& pairs, double, std::uint64_t) const override
		{
			++geometryScript().calls;
			geometryScript().handed = pairs.size();
			return geometryScript().candidates;
		}
		std::vector<math::RigidTransformd> absolutePoses(const std::vector<PointRay>& pointRays, double,
														 std::uint64_t) const override
		{
			++geometryScript().calls;
			geometryScript().handed = pointRays.size();
			return geometryScript().candidates;
		}
	};

	void ensureStandIns()
	{
		static const bool once = []
		{
			extractorRegistry().registerType<ScriptedExtractor>("scripted");
			matcherRegistry().registerType<ExactMatcher>("a-exact");
			matcherRegistry().registerType<BothMatcher>("b-both");
			matcherRegistry().registerType<BrokenMatcher>("c-broken");
			geometryRegistry().registerType<ScriptedGeometry>("scripted");
			return true;
		}();
		(void)once;
	}

	// --- scenes -----------------------------------------------------------------

	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		return 2.0 * std::acos(std::min(1.0, std::abs(math::dot(a.rotation(), b.rotation()))));
	}

	class Random
	{
	public:
		explicit Random(std::uint64_t seed)
			: m_engine(seed)
		{
		}
		double uniform(double lo, double hi) { return lo + (hi - lo) * double(m_engine() >> 11) * 0x1.0p-53; }

	private:
		std::mt19937_64 m_engine;
	};

	// Two cameras half a metre apart, B turned 0.2 rad against A, and `near` points 3 to 8 m in front
	// of both; then `far` points 100 km away, whose parallax (5 µrad) is far below any inlier angle.
	struct TwoView
	{
		math::RigidTransformd bFromA;
		std::vector<RayPair> pairs;
	};
	TwoView twoView(std::size_t near, std::size_t far = 0, std::uint64_t seed = 1)
	{
		TwoView out;
		const math::Vec3d centreB{0.5, 0.05, 0.1};
		const math::Quatd bFromARotation = math::angleAxis(0.2, math::normalize(math::Vec3d{0.3, 1.0, 0.1}));
		out.bFromA = math::RigidTransformd{bFromARotation, -(bFromARotation * centreB)};
		// A unit baseline, as relativePose reports it.
		out.bFromA = math::RigidTransformd{out.bFromA.rotation(), math::normalize(out.bFromA.translation())};
		Random random(seed);
		const auto add = [&](double depth)
		{
			const math::Vec3d p{random.uniform(-0.3, 0.3) * depth, random.uniform(-0.2, 0.2) * depth, depth};
			const math::Vec3d inB = out.bFromA.apply(p / math::length(centreB));
			REQUIRE(inB.z > 0);
			out.pairs.push_back({math::normalize(p), math::normalize(inB)});
		};
		for (std::size_t i = 0; i < near; ++i)
			add(random.uniform(3.0, 8.0));
		for (std::size_t i = 0; i < far; ++i)
			add(1e5);
		return out;
	}

	// The four relative poses one essential matrix decomposes into: the rotation and its twisted pair
	// (turned half a revolution about the baseline), each with the baseline either way.
	std::vector<math::RigidTransformd> decompositions(const math::RigidTransformd& truth)
	{
		const math::Vec3d t = truth.translation();
		const math::Quatd twisted = math::angleAxis(3.141592653589793, math::normalize(t)) * truth.rotation();
		return {truth, {truth.rotation(), -t}, {twisted, t}, {twisted, -t}};
	}

	bool same(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		return rotationBetween(a, b) < 1e-9 && math::length(a.translation() - b.translation()) < 1e-9;
	}

	std::vector<std::uint32_t> upTo(std::size_t n)
	{
		std::vector<std::uint32_t> out;
		for (std::size_t i = 0; i < n; ++i)
			out.push_back(std::uint32_t(i));
		return out;
	}
} // namespace

// --- extraction ---------------------------------------------------------------------

TEST_CASE("extract states the image and the scale itself", "[camera][feature]")
{
	ensureStandIns();
	Features answer = toy({1, 2, 3});
	answer.image = {1, 1}; // what a careless backend might say
	answer.scale = 0.123;
	answer.keypoints[2].pixel = {3999.0, 299.0};
	extractorScript().answer = answer;
	const ExtractResult result = extract(image::Image{4000, 300, image::PixelFormat::Gray8}, LongestSide{1920});
	REQUIRE(result.ok());
	CHECK(extractorScript().askedScale == 0.48);
	CHECK(result.features.scale == 0.48);
	CHECK(result.features.image == ImageGeometry{4000, 300});
	CHECK(result.features.size() == 3);
	CHECK(result.provenance.backend == "scripted");
}

TEST_CASE("extract refuses an image the backend cannot read", "[camera][feature]")
{
	ensureStandIns();
	extractorScript().answer = toy({1});
	SECTION("an empty image")
	{
		const ExtractResult result = extract(image::Image{});
		CHECK(result.status == Status::Unsupported);
		CHECK_THAT(result.detail, ContainsSubstring("empty"));
	}
	SECTION("a format the backend refuses")
	{
		const ExtractResult result = extract(image::Image{64, 48, image::PixelFormat::Gray16});
		CHECK(result.status == Status::Unsupported);
		CHECK_THAT(result.detail, ContainsSubstring("Gray16"));
	}
}

TEST_CASE("extract refuses an answer that breaks the extractor's contract", "[camera][feature]")
{
	ensureStandIns();
	Features answer = toy({1, 2, 3});
	SECTION("descriptors that do not fit the keypoints")
	{
		answer.descriptors.pop_back();
	}
	SECTION("a keypoint off the source image")
	{
		answer.keypoints[1].pixel = {64.0, 0.0}; // the last pixel centre is 63, its edge 63.5
	}
	SECTION("a keypoint that is not finite")
	{
		answer.keypoints[0].response = kNaN;
	}
	SECTION("no descriptor kind")
	{
		answer.kind.clear();
	}
	extractorScript().answer = answer;
	const ExtractResult result = extract(image::Image{64, 48, image::PixelFormat::Gray8});
	CHECK(result.status == Status::BackendMisbehaved);
	CHECK_THAT(result.detail, ContainsSubstring("scripted"));
	CHECK(result.features.size() == 0);
}

// --- matching -------------------------------------------------------------------------

TEST_CASE("a match must be clearly nearer than the second nearest", "[camera][feature][match]")
{
	ensureStandIns();
	// a0 is b0 exactly, and far from b1; a1 is b1 and b2 exactly, a tie.
	const MatchResult result = match(toy({10, 50}), toy({10, 50, 50}));
	REQUIRE(result.ok());
	REQUIRE(result.matches.size() == 1);
	CHECK(result.matches[0].a == 0);
	CHECK(result.matches[0].b == 0);
	CHECK(result.counts.candidates == 2);
	CHECK(result.counts.ambiguous == 1);
	CHECK(result.counts.notMutual == 0);
}

TEST_CASE("a match must be each feature's nearest both ways", "[camera][feature][match]")
{
	ensureStandIns();
	// b0 (10) is the nearest of a0 (0), a1 (9) and a2 (100), but only a1 is b0's nearest.
	const MatchResult result = match(toy({0, 9, 100}), toy({10, 200}));
	REQUIRE(result.ok());
	REQUIRE(result.matches.size() == 1);
	CHECK(result.matches[0].a == 1);
	CHECK(result.matches[0].b == 0);
	CHECK(result.counts.notMutual == 2);
}

TEST_CASE("matching a with b is matching b with a", "[camera][feature][match]")
{
	ensureStandIns();
	const auto swapped = [](const MatchResult& r)
	{
		std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
		for (const Match& m : r.matches)
			out.emplace_back(m.b, m.a);
		std::sort(out.begin(), out.end());
		return out;
	};
	const auto pairs = [](const MatchResult& r)
	{
		std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
		for (const Match& m : r.matches)
			out.emplace_back(m.a, m.b);
		return out;
	};
	SECTION("where only the second image's ratio test rejects")
	{
		// a0 (0) is clearly nearest b0 (10, against 30), but b0 is nearly as near a1 (21) as a0.
		const Features a = toy({0, 21}), b = toy({10, 30});
		const MatchResult ab = match(a, b), ba = match(b, a);
		CHECK(ab.matches.empty());
		CHECK(ab.counts.ambiguous == 2);
		CHECK(pairs(ba) == swapped(ab));
	}
	SECTION("over many features")
	{
		Random random(5);
		std::vector<int> av, bv;
		for (int i = 0; i < 60; ++i)
			av.push_back(int(random.uniform(0, 255)));
		for (int i = 0; i < 70; ++i)
			bv.push_back(int(random.uniform(0, 255)));
		const MatchResult ab = match(toy(av), toy(bv)), ba = match(toy(bv), toy(av));
		// Measured: 15 matches, 16 ambiguous and 29 not mutual, so agreement is not the agreement of two
		// empty lists, and both tests reject something.
		CHECK(ab.matches.size() >= 10);
		CHECK(ab.counts.ambiguous > 0);
		CHECK(ab.counts.notMutual > 0);
		CHECK(pairs(ba) == swapped(ab));
	}
}

TEST_CASE("match is answered by the first matcher that takes the kind and the search", "[camera][feature][match]")
{
	ensureStandIns();
	const Features a = toy({0, 100}), b = toy({0, 100});
	CHECK(match(a, b, {MatchSearch::Exact, 0.8}).provenance.backend == "a-exact");
	CHECK(match(a, b, {MatchSearch::Approximate, 0.8}).provenance.backend == "b-both");
	CHECK(match(a, b, {MatchSearch::Exact, 1.0}).ok());
}

TEST_CASE("match refuses what no matcher can do", "[camera][feature][match]")
{
	ensureStandIns();
	MatchResult result;
	std::string reason;
	SECTION("a kind no matcher accepts")
	{
		result = match(toy({0, 1}, "unknown"), toy({0, 1}, "unknown"));
		reason = "no matcher accepts 'unknown'";
	}
	SECTION("a search no matcher of the kind supports")
	{
		result = match(toy({0, 1}, "exactonly"), toy({0, 1}, "exactonly"), {MatchSearch::Approximate, 0.8});
		reason = "supports Approximate";
	}
	SECTION("two kinds")
	{
		result = match(toy({0, 1}), toy({0, 1}, "exactonly"));
		reason = "cannot be matched";
	}
	SECTION("two descriptor sizes")
	{
		Features wide = toy({0, 1});
		wide.descriptorBytes = 2;
		wide.descriptors = {0, 0, 1, 1};
		result = match(toy({0, 1}), wide);
		reason = "cannot be matched";
	}
	SECTION("a ratio outside (0, 1]")
	{
		result = match(toy({0, 1}), toy({0, 1}), {MatchSearch::Exact, 1.5});
		reason = "outside (0, 1]";
		CHECK(match(toy({0, 1}), toy({0, 1}), {MatchSearch::Exact, 0.0}).status == Status::Unsupported);
		CHECK(match(toy({0, 1}), toy({0, 1}), {MatchSearch::Exact, kNaN}).status == Status::Unsupported);
	}
	CHECK(result.status == Status::Unsupported);
	CHECK_THAT(result.detail, ContainsSubstring(reason));
}

TEST_CASE("match needs a second neighbour on each side", "[camera][feature][match]")
{
	ensureStandIns();
	const MatchResult result = match(toy({5}), toy({0, 1, 2}));
	CHECK(result.status == Status::TooFew);
	CHECK(result.provenance.backend == "a-exact");
}

TEST_CASE("match refuses an answer that breaks the matcher's contract", "[camera][feature][match]")
{
	ensureStandIns();
	SECTION("one answer too few")
	{
		brokenScript() = Broken::WrongCount;
	}
	SECTION("a neighbour out of range")
	{
		brokenScript() = Broken::OutOfRange;
	}
	SECTION("one neighbour twice")
	{
		brokenScript() = Broken::Repeated;
	}
	SECTION("the second nearer than the first")
	{
		brokenScript() = Broken::Unordered;
	}
	const MatchResult result = match(toy({0, 50, 100}, "broken"), toy({0, 50, 100}, "broken"));
	CHECK(result.status == Status::BackendMisbehaved);
	CHECK_THAT(result.detail, ContainsSubstring("c-broken"));
	CHECK(result.matches.empty());
}

// --- relative pose ----------------------------------------------------------------------

TEST_CASE("relativePose chooses by measurement, whatever order the candidates come in", "[camera][feature][geometry]")
{
	ensureStandIns();
	const TwoView scene = twoView(40);
	const std::vector<math::RigidTransformd> four = decompositions(scene.bFromA);
	std::vector<std::size_t> order{0, 1, 2, 3};
	do
	{
		std::vector<math::RigidTransformd> candidates;
		for (const std::size_t i : order)
			candidates.push_back(four[i]);
		propose(candidates);
		const GeometryResult result = relativePose(scene.pairs);
		REQUIRE(result.ok());
		CHECK(same(*result.pose, scene.bFromA));
		CHECK(result.inliers == upTo(40));
		CHECK(result.candidates == 4);
		// Exact rays: measured 3e-15 rad.
		CHECK(result.rmsAngle < 1e-12);
	} while (std::next_permutation(order.begin(), order.end()));
}

TEST_CASE("relativePose counts its own inliers, not the pairs the backend fitted", "[camera][feature][geometry]")
{
	ensureStandIns();
	// 60 pairs, every third one's B ray turned 0.1 rad off its epipolar plane: an outlier, whichever
	// pose the backend believed explained it.
	TwoView scene = twoView(60);
	std::vector<std::uint32_t> clean;
	const math::Vec3d t = scene.bFromA.translation();
	for (std::size_t k = 0; k < scene.pairs.size(); ++k)
	{
		if (k % 3 != 0)
		{
			clean.push_back(std::uint32_t(k));
			continue;
		}
		RayPair& p = scene.pairs[k];
		const math::Vec3d normal = math::normalize(math::cross(t, scene.bFromA.rotate(p.a)));
		p.b = math::angleAxis(0.1, math::normalize(math::cross(p.b, normal))) * p.b;
	}
	propose({scene.bFromA});
	const GeometryResult result = relativePose(scene.pairs);
	REQUIRE(result.ok());
	CHECK(result.inliers == clean);
	// The outliers take no part: measured 3e-15 rad over the clean pairs.
	CHECK(result.rmsAngle < 1e-12);
}

TEST_CASE("relativePose takes a pair with no parallax as a point at infinity", "[camera][feature][geometry]")
{
	ensureStandIns();
	SECTION("far points are inliers when the rotation agrees")
	{
		const TwoView scene = twoView(20, 20);
		propose({scene.bFromA});
		const GeometryResult result = relativePose(scene.pairs);
		REQUIRE(result.ok());
		CHECK(result.inliers == upTo(40));
	}
	SECTION("and outliers when it does not")
	{
		// Turned 20 mrad about the baseline, which moves every B ray out of its epipolar plane, so no
		// depth explains a far pair. (Turned about an axis IN the epipolar plane, a far pair is simply
		// triangulated nearer and is an inlier, as two-view geometry says it should be.)
		const TwoView scene = twoView(20, 20);
		const math::Vec3d baseline = math::normalize(scene.bFromA.translation());
		const math::RigidTransformd turned{math::angleAxis(0.02, baseline) * scene.bFromA.rotation(),
										   scene.bFromA.translation()};
		propose({turned});
		const GeometryResult result = relativePose(scene.pairs);
		// Measured: it explains none of the 40, near or far.
		CHECK(result.status == Status::NoSolution);
		CHECK_THAT(result.detail, ContainsSubstring("explains 0 of 40"));
	}
	SECTION("near points decide the baseline's direction")
	{
		const TwoView scene = twoView(20, 20);
		propose({{scene.bFromA.rotation(), -scene.bFromA.translation()}, scene.bFromA});
		const GeometryResult result = relativePose(scene.pairs);
		REQUIRE(result.ok());
		CHECK(same(*result.pose, scene.bFromA));
	}
}

TEST_CASE("candidates that tie are chosen the same in either order", "[camera][feature][geometry]")
{
	ensureStandIns();
	// Only far points: the baseline either way explains them identically.
	const TwoView scene = twoView(0, 20);
	const math::RigidTransformd forward = scene.bFromA;
	const math::RigidTransformd backward{scene.bFromA.rotation(), -scene.bFromA.translation()};
	propose({forward, backward});
	const GeometryResult first = relativePose(scene.pairs);
	propose({backward, forward});
	const GeometryResult second = relativePose(scene.pairs);
	REQUIRE(first.ok());
	REQUIRE(second.ok());
	CHECK(first.inliers == upTo(20));
	CHECK(same(*first.pose, *second.pose));
}

TEST_CASE("relativePose's inliers index the input", "[camera][feature][geometry]")
{
	ensureStandIns();
	const TwoView scene = twoView(10);
	std::vector<RayPair> input;
	std::vector<std::uint32_t> expected;
	for (const RayPair& p : scene.pairs)
	{
		input.push_back({math::Vec3d{0.0}, p.b});			 // no ray
		input.push_back({math::Vec3d{kNaN, 0.0, 1.0}, p.b}); // not a ray
		expected.push_back(std::uint32_t(input.size()));
		input.push_back({3.0 * p.a, p.b}); // a ray, not unit length
	}
	propose({scene.bFromA});
	const GeometryResult result = relativePose(input);
	REQUIRE(result.ok());
	CHECK(geometryScript().handed == 10);
	CHECK(result.inliers == expected);
}

TEST_CASE("relativePose needs enough pairs and a candidate that explains them", "[camera][feature][geometry]")
{
	ensureStandIns();
	const TwoView scene = twoView(20);
	SECTION("four usable pairs are too few, and the backend is never asked")
	{
		propose({scene.bFromA});
		std::vector<RayPair> four(scene.pairs.begin(), scene.pairs.begin() + 4);
		four.push_back({math::Vec3d{0.0}, math::Vec3d{0.0, 0.0, 1.0}});
		const GeometryResult result = relativePose(four);
		CHECK(result.status == Status::TooFew);
		CHECK(geometryScript().calls == 0);
	}
	SECTION("no candidate")
	{
		propose({});
		CHECK(relativePose(scene.pairs).status == Status::NoSolution);
	}
	SECTION("only wrong candidates")
	{
		propose({{math::angleAxis(0.3, math::Vec3d{1.0, 0.0, 0.0}) * scene.bFromA.rotation(), scene.bFromA.translation()}});
		const GeometryResult result = relativePose(scene.pairs);
		CHECK(result.status == Status::NoSolution);
		CHECK_THAT(result.detail, ContainsSubstring("of 20 usable pairs"));
	}
	SECTION("a candidate with no baseline is skipped")
	{
		propose({{scene.bFromA.rotation(), math::Vec3d{0.0}}});
		const GeometryResult result = relativePose(scene.pairs);
		CHECK(result.status == Status::NoSolution);
		CHECK(result.candidates == 1);
	}
	SECTION("an inlier angle that is not positive")
	{
		propose({scene.bFromA});
		CHECK(relativePose(scene.pairs, {0.0, 0}).status == Status::Unsupported);
	}
}

// --- absolute pose ------------------------------------------------------------------

namespace
{
	// A camera 2 m from the reference origin looking back at it, and points within a metre of the
	// origin.
	struct OneView
	{
		math::RigidTransformd cameraFromReference;
		std::vector<PointRay> pointRays;
	};
	OneView oneView(std::size_t count)
	{
		OneView out;
		const math::Quatd rotation = math::angleAxis(0.3, math::normalize(math::Vec3d{0.2, 1.0, -0.4}));
		out.cameraFromReference = math::RigidTransformd{rotation, math::Vec3d{0.1, -0.05, 2.0}};
		Random random(3);
		for (std::size_t i = 0; i < count; ++i)
		{
			const math::Vec3d p{random.uniform(-0.8, 0.8), random.uniform(-0.6, 0.6), random.uniform(-0.5, 0.5)};
			const math::Vec3d inCamera = out.cameraFromReference.apply(p);
			REQUIRE(inCamera.z > 0);
			out.pointRays.push_back({p, math::normalize(inCamera)});
		}
		return out;
	}
} // namespace

TEST_CASE("absolutePose chooses by measurement, whatever order the candidates come in", "[camera][feature][geometry]")
{
	ensureStandIns();
	const OneView scene = oneView(30);
	const math::RigidTransformd& truth = scene.cameraFromReference;
	// Another of P3P's solutions (a pose fitting a few points, not all), and the truth turned half a
	// revolution, with every point behind it.
	const math::RigidTransformd other{math::angleAxis(0.4, math::Vec3d{0.0, 1.0, 0.0}) * truth.rotation(),
									  truth.translation() + math::Vec3d{0.3, 0.0, 0.1}};
	const math::Quatd half = math::angleAxis(3.141592653589793, math::Vec3d{0.0, 1.0, 0.0});
	const math::RigidTransformd behind{half * truth.rotation(), half * truth.translation()};
	const std::vector<math::RigidTransformd> three{truth, other, behind};
	std::vector<std::size_t> order{0, 1, 2};
	do
	{
		std::vector<math::RigidTransformd> candidates;
		for (const std::size_t i : order)
			candidates.push_back(three[i]);
		propose(candidates);
		const GeometryResult result = absolutePose(scene.pointRays);
		REQUIRE(result.ok());
		CHECK(same(*result.pose, truth));
		CHECK(result.inliers == upTo(30));
	} while (std::next_permutation(order.begin(), order.end()));

	propose({behind});
	CHECK(absolutePose(scene.pointRays).status == Status::NoSolution);
}

TEST_CASE("absolutePose counts its own inliers", "[camera][feature][geometry]")
{
	ensureStandIns();
	OneView scene = oneView(30);
	std::vector<std::uint32_t> clean;
	for (std::size_t k = 0; k < scene.pointRays.size(); ++k)
	{
		PointRay& p = scene.pointRays[k];
		if (k % 3 == 0)
			p.ray = math::angleAxis(0.05, math::normalize(math::cross(p.ray, math::Vec3d{1.0, 0.0, 0.0}))) * p.ray;
		else if (k == 10)
			p.ray = -p.ray; // the point is on the ray's line, but behind the camera
		else
			clean.push_back(std::uint32_t(k));
	}
	propose({scene.cameraFromReference});
	const GeometryResult result = absolutePose(scene.pointRays);
	REQUIRE(result.ok());
	CHECK(result.inliers == clean);
}

TEST_CASE("absolutePose needs four usable point-rays", "[camera][feature][geometry]")
{
	ensureStandIns();
	OneView scene = oneView(4);
	scene.pointRays[2].point.x = kNaN;
	propose({scene.cameraFromReference});
	const GeometryResult result = absolutePose(scene.pointRays);
	CHECK(result.status == Status::TooFew);
	CHECK_THAT(result.detail, ContainsSubstring("3 usable point-rays"));
	CHECK(geometryScript().calls == 0);
}
