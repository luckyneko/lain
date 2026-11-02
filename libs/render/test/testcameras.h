#pragma once

// Strongly distorted cameras, one per distortion model, for the capture pass's tests: the ray table's
// here, the device pass's, and the board claim's in plugins/camera/test. Free of any test framework.
//
// At scale 1 each is 640x480 with every intrinsic distinct (fx != fy, the principal point off the
// centre), so a transposed parameter moves the answer. The coefficients are strong where
// testcameraparameters.h's are mild, since the capture pass has to bend straight lines visibly:
// Brown-Conrady's corner sits at a distorted radius of 1 with a k2 that keeps it from folding, and
// the fisheye sees about 168 degrees across its diagonal. A scale keeps the field of view and changes
// the resolution, through camera::scaled, as looking through a camera at a reduced resolution does.

#include <lain/camera/cameramodel.h>

#include <cstdint>

namespace lain::render::testing
{
	inline constexpr int kStrongModelCount = 6;

	inline camera::CameraModelParameters strongCamera(int index, double scale = 1)
	{
		const double f = index == 5 ? 260 : 400;
		camera::CameraModelParameters p;
		p.image = {640, 480};
		p.intrinsics = {f, 1.01 * f, 330.25, 236.75};
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
		if (scale == 1)
			return p;
		const camera::ImageGeometry image{std::uint32_t(640 * scale), std::uint32_t(480 * scale)};
		return camera::scaled(*camera::CameraModel::create(p).model, image).model->parameters();
	}
} // namespace lain::render::testing
