#pragma once

#include <lain/camera/board/detection.h>

#include <algorithm>
#include <cstdint>

namespace lain::camera::detail
{
	// A detection request from the three settings detectBoard and calibrateCamera both expose, so the
	// two mean the same thing by them.
	inline board::DetectionRequest detectionRequest(int longestSide, bool refineNative, int minimumCorners)
	{
		board::DetectionRequest request;
		if (longestSide > 0)
			request.scale = board::LongestSide{std::uint32_t(longestSide)};
		request.refineAtNativeResolution = refineNative;
		request.minimumCorners = std::uint32_t(std::max(0, minimumCorners));
		return request;
	}
} // namespace lain::camera::detail
