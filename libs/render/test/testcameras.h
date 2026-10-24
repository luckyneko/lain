#pragma once

// Strongly distorted cameras, one per distortion model, for the capture pass's tests: the ray table's
// here, the device pass's, and the board claim's in plugins/camera/test. Free of any test framework.
//
// At scale 1 each is 640x480 with every intrinsic distinct (fx != fy, the principal point off the
// centre), so a transposed parameter moves the answer. The coefficients are strong where
// testcameraparameters.h's are mild, since the capture pass has to bend straight lines visibly:
// Brown-Conrady's corner sits at a distorted radius of 1 with a k2 that keeps it from folding, and
// the fisheye sees about 168 degrees across its diagonal. A scale keeps the field of view and changes
// the resolution.

#include <lain/camera/cameramodel.h>

#include <cstdint>

namespace lain::render::testing
{
	inline constexpr int kStrongModelCount = 6;

	inline camera::CameraModelParameters strongCamera(int index, double scale = 1)
	{
		const double f = index == 5 ? 260 : 400;
		camera::CameraModelParameters p;
		p.image = {std::uint32_t(640 * scale), std::uint32_t(480 * scale)};
		// A pixel's centre scales about the image's corner: u + 0.5 is the distance to its left edge.
		p.intrinsics = {f * scale, 1.01 * f * scale, (330.25 + 0.5) * scale - 0.5, (236.75 + 0.5) * scale - 0.5};
		switch (index)
		{
			case 0:
				p.distortion = camera::NoDistortion{};
				break;
			case 1:
				p.distortion = camera::BrownConrady5{-0.25, 0.05, 0.001, -0.001, 0.0};
				break;
			case 2:
				p.distortion = camera::InverseBrownConrady5{-0.2, 0.05, 0.001, -0.001, 0.0};
				break;
			case 3:
				p.distortion = camera::ModifiedBrownConrady5{-0.25, 0.05, 0.001, -0.001, 0.0};
				break;
			case 4:
				p.distortion = camera::RationalBrownConrady8{-0.25, 0.05, 0.001, -0.001, 0.0, 0.05, 0.0, 0.0};
				break;
			default:
				p.distortion = camera::KannalaBrandt4{0.02, -0.005, 0.001, 0.0};
				break;
		}
		return p;
	}
} // namespace lain::render::testing
