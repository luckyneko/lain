#pragma once

namespace lain::camera::ceres
{
	// Register this plugin's backends under the key "ceres". Nothing yet: the registration refiner
	// arrives with M9 slice 2, sub-slice 5. Called by the generated
	// lain::camera::registerCameraBackends(), never directly.
	void registerBackend();
} // namespace lain::camera::ceres
