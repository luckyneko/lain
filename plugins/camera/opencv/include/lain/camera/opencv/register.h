#pragma once

namespace lain::camera::opencv
{
	// Register this plugin's backends under the key "opencv": the board renderer, detector and pose
	// solver, the calibration estimator, and the feature extractor (SIFT), matcher and geometry
	// solver. It also caps OpenCV's own thread pool at one thread, since the process pool is the only
	// one (ADR-0024) and lain parallelises across frames and camera pairs itself (ADR-0016). Called by
	// the generated lain::camera::registerCameraBackends(), never directly.
	void registerBackend();
} // namespace lain::camera::opencv
