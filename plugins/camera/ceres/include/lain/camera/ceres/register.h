#pragma once

namespace lain::camera::ceres
{
	// Register this plugin's backends under the key "ceres": the registration refiner. Called by the
	// generated lain::camera::registerCameraBackends(), never directly.
	void registerBackend();
} // namespace lain::camera::ceres
