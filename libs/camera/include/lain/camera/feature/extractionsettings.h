#pragma once

#include "lain/camera/feature/matching.h"
#include "lain/camera/method.h" // ExecutionPolicy
#include "lain/camera/scalepolicy.h"

#include <cstdint>

// What every feature-track extraction reads, the fixed rig's (extraction.h) and, from M9 slice 4's
// sub-slice 7c, a moving camera's: how each image's features are found and capped, how two images'
// features are matched and pre-screened, how many inliers verify a pair, and how the work runs. Each
// extraction's request derives from it and adds what only it reads, so a default measured here is
// stated once.
namespace lain::camera::feature
{
	struct ExtractionSettings
	{
		// The scale each frame is searched at.
		ScalePolicy scale = LongestSide{1920};
		// The strongest features of each frame kept, each with all its orientations.
		std::uint32_t featureCap = 8192;
		// How features are matched: between a camera's frames, and between cameras.
		MatchRequest matching;
		// A pair verifies with at least this many inliers (COLMAP's two-view minimum); at least the
		// fewest its verification needs.
		std::uint32_t minimumPairInliers = 15;
		// A pair is first matched on each side's strongest few features, and skipped when fewer than
		// `prescreenMatches` survive.
		std::uint32_t prescreenFeatures = 512;
		std::uint32_t prescreenMatches = 5;
		// How precisely a feature is located: a standard deviation per unit of its size
		// (Keypoint::size), so in source pixels whatever the scale searched. A detector finds a
		// feature at a size it chose, and a larger one is located less precisely. 0.034 is SIFT's,
		// measured against the truth on rendered scenes (M9 slice 3, sub-slice 7): 3.4% natively,
		// within the same bound at half scale.
		double localisationPerSize = 0.034;
		ExecutionPolicy execution = ExecutionPolicy::Normal;
	};
} // namespace lain::camera::feature
