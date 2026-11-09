#pragma once

// A camera for the tests: 640x480, with every intrinsic distinct (fx != fy, cx != cy, none a round
// half of the image) so a transposed parameter moves the answer instead of hiding. Its parameters and
// distortions are testcameraparameters.h's, which needs no test framework.

#include "testcameraparameters.h"

#include <lain/camera/cameramodel.h>

#include <catch2/catch_test_macros.hpp>

namespace lain::camera::testing
{
	inline CameraModel cameraWith(const Distortion& distortion)
	{
		ModelResult result = CameraModel::create(parametersWith(distortion));
		REQUIRE(result.model.has_value());
		return *result.model;
	}
} // namespace lain::camera::testing
