#pragma once

#include <lain/camera/calibration/estimator.h>

namespace lain::camera::opencv
{
	// Camera calibration with OpenCV: calibrateCamera for no distortion, Brown-Conrady 5 and the
	// rational 8-coefficient model, cv::fisheye::calibrate for Kannala-Brandt 4. Inverse and modified
	// Brown-Conrady are representable in lain and estimated by nothing here, so asking for them is a
	// failed report, never a substitution.
	//
	// Board poses under a fixed model work for EVERY model, OpenCV's or not: each corner is
	// unprojected with lain's own camera model, and the pose is solved on the resulting pinhole
	// normalised coordinates. So validation never depends on OpenCV implementing the model.
	class OpenCVEstimator : public calibration::Estimator
	{
	public:
		Provenance provenance() const override;
		bool canEstimate(DistortionModel model) const override;
		calibration::Estimate estimate(const ImageGeometry& image, const std::vector<board::Observation>& views,
									   const board::Specification& board, DistortionModel model,
									   const std::optional<CameraModelParameters>& initial) const override;
		std::optional<math::RigidTransformd> boardPose(const CameraModel& model, const board::Specification& board,
													   const board::Observation& view) const override;
	};
} // namespace lain::camera::opencv
