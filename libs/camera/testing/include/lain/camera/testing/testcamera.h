#pragma once

// A camera for the tests: 640x480, with every intrinsic distinct (fx != fy, cx != cy, none a round
// half of the image) so a transposed parameter moves the answer instead of hiding; and the distortions
// the tests use.

#include <lain/camera/cameramodel.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace lain::camera::testing
{
	inline CameraModelParameters parametersWith(const Distortion& distortion)
	{
		CameraModelParameters p;
		p.image = {640, 480};
		p.intrinsics = {500.0, 510.0, 320.5, 239.5};
		p.distortion = distortion;
		return p;
	}

	// Distinct coefficients, of both signs and different magnitudes, so swapping any two changes the
	// projection.
	inline BrownConrady5 brownConrady() { return {0.1, -0.05, 0.001, -0.002, 0.01}; }
	inline InverseBrownConrady5 inverseBrownConrady() { return {0.1, -0.05, 0.001, -0.002, 0.01}; }
	inline ModifiedBrownConrady5 modifiedBrownConrady() { return {0.1, -0.05, 0.001, -0.002, 0.01}; }
	inline RationalBrownConrady8 rational() { return {0.1, -0.05, 0.001, -0.002, 0.01, 0.02, -0.01, 0.005}; }
	inline KannalaBrandt4 kannalaBrandt() { return {0.05, -0.01, 0.002, -0.0005}; }

	inline Distortion everyModel(int index)
	{
		switch (index)
		{
			case 0:
				return NoDistortion{};
			case 1:
				return brownConrady();
			case 2:
				return inverseBrownConrady();
			case 3:
				return modifiedBrownConrady();
			case 4:
				return rational();
			default:
				return kannalaBrandt();
		}
	}
	constexpr int kModelCount = 6;

	inline CameraModel cameraWith(const Distortion& distortion)
	{
		ModelResult result = CameraModel::create(parametersWith(distortion));
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	// A camera of `width` x `height` with the test camera's field of view (f 500 at 640 wide), its
	// principal point in the middle, through `distortion`: for the large-image cases, and for the
	// distortion sweep, where one k1 is a different lens at each field of view.
	inline CameraModel largeCamera(std::uint32_t width, std::uint32_t height, const Distortion& distortion = NoDistortion{})
	{
		CameraModelParameters p = parametersWith(distortion);
		const double f = 500.0 * double(width) / 640.0;
		p.image = {width, height};
		p.intrinsics = {f, f, 0.5 * double(width) - 0.5, 0.5 * double(height) - 0.5};
		ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}
} // namespace lain::camera::testing
