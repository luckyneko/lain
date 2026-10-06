#pragma once

#include "lain/camera/cameramodel.h" // ImageGeometry

#include <cstdint>
#include <variant>

namespace lain::camera
{
	// The image scale to look for features at (CONTEXT.md, "Detection scale policy"): a board's
	// corners, or a scene's. It changes the cost and maybe the accuracy of finding features, and
	// never the coordinate system of what is found, which is always the source image's.
	struct NativeScale // the source resolution
	{
	};
	struct ScaleFactor // a fixed factor in (0, 1]; larger is taken as 1
	{
		double factor = 1.0;
	};
	struct LongestSide // at most this many pixels on the longer side; never enlarged
	{
		std::uint32_t pixels = 1920;
	};
	using ScalePolicy = std::variant<NativeScale, ScaleFactor, LongestSide>;

	// The factor a policy asks for on an image of this geometry, in (0, 1].
	double resolveScale(const ScalePolicy& policy, const ImageGeometry& image);
} // namespace lain::camera
