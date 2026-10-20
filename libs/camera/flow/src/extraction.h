#pragma once

#include "decimal.h"

#include <lain/camera/feature/extractionsettings.h>

#include <algorithm>
#include <cstdint>

namespace lain::camera::detail
{
	// The extraction settings both targetless nodes expose, written into `settings` (a fixed rig's
	// extraction request or a moving camera's, each deriving from them), so the two mean the same
	// thing by them: the scale each frame is searched at (0 for native), the strongest features kept,
	// how features are matched, and the inliers a pair needs to verify. A value the method cannot use
	// is refused there, with the reason, rather than quietly corrected here.
	inline void extractionSettings(feature::ExtractionSettings& settings, int longestSide, int featureCap,
								   feature::MatchSearch matchSearch, float matchRatio, int minimumPairInliers)
	{
		if (longestSide > 0)
			settings.scale = LongestSide{std::uint32_t(longestSide)};
		else
			settings.scale = NativeScale{};
		settings.featureCap = std::uint32_t(std::max(0, featureCap));
		settings.matching.search = matchSearch;
		settings.matching.ratio = decimal(matchRatio);
		settings.minimumPairInliers = std::uint32_t(std::max(0, minimumPairInliers));
	}
} // namespace lain::camera::detail
