#include "lain/camera/opencv/register.h"

#include "charucodetector.h"
#include "charucorenderer.h"
#include "opencvestimator.h"
#include "opencvgeometry.h"
#include "opencvmatcher.h"
#include "opencvposesolver.h"
#include "siftextractor.h"

#include <opencv2/core/utility.hpp>

namespace lain::camera::opencv
{
	void registerBackend()
	{
		// One pool per process (ADR-0024): OpenCV's own is capped at the calling thread, and lain
		// runs frames in parallel on the process pool instead (ADR-0016). Set here, once, because
		// this is where the plugin joins the process.
		cv::setNumThreads(0);

		board::rendererRegistry().registerType<CharucoRenderer>("opencv");
		board::detectorRegistry().registerType<CharucoDetector>("opencv");
		calibration::estimatorRegistry().registerType<OpenCVEstimator>("opencv");
		board::poseSolverRegistry().registerType<OpenCVPoseSolver>("opencv");
		feature::extractorRegistry().registerType<SiftExtractor>("opencv");
		feature::matcherRegistry().registerType<OpenCVMatcher>("opencv");
		feature::geometryRegistry().registerType<OpenCVGeometrySolver>("opencv");
	}
} // namespace lain::camera::opencv
