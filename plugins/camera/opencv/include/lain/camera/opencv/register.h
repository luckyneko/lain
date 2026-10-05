#pragma once

namespace lain::camera::opencv
{
	// Register this plugin's backends under the key "opencv": the board renderer, the board detector,
	// the board pose solver and the calibration estimator. It also caps OpenCV's own thread pool at one thread, since the process pool is the
	// only one (ADR-0024) and lain parallelises across frames itself (ADR-0016). Called by the
	// generated lain::camera::registerCameraBackends(), never directly.
	void registerBackend();
} // namespace lain::camera::opencv
