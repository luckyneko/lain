#pragma once

// A camera for the tests: 640x480, with every intrinsic distinct (fx != fy, cx != cy, none a round
// half of the image) so a transposed parameter moves the answer instead of hiding. Its parameters and
// distortions are testcameraparameters.h's, which needs no test framework.

#include "testcameraparameters.h"

#include <lain/camera/cameramodel.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace lain::camera::testing
{
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
