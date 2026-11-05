#pragma once

#include <lain/camera/calibration/estimator.h>

namespace lain::camera::opencv
{
	// Camera calibration with OpenCV: calibrateCamera for no distortion, Brown-Conrady 5 and the
	// rational 8-coefficient model, cv::fisheye::calibrate for Kannala-Brandt 4. Inverse and modified
	// Brown-Conrady are representable in lain and estimated by nothing here, so asking for them is a
	// failed report, never a substitution. Board poses under a fixed model are the pose solver's
	// (opencvposesolver.h), which works for every model.
	class OpenCVEstimator : public calibration::Estimator
	{
	public:
		Provenance provenance() const override;
		bool canEstimate(DistortionModel model) const override;
		calibration::Estimate estimate(const ImageGeometry& image, const std::vector<board::Observation>& views,
									   const board::Specification& board, DistortionModel model,
									   const std::optional<CameraModelParameters>& initial) const override;
	};
} // namespace lain::camera::opencv
