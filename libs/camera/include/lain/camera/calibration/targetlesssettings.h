#pragma once

#include "lain/camera/feature/sequenceextraction.h"
#include "lain/camera/refinement/refiner.h"

// What only targetless calibration is asked (calibration/targetless.h), apart from the request every
// calibration method shares: an argument of its entry points, as board calibration's detection request
// is of board's. Its own header, so a calibration report can record it as run.
namespace lain::camera::calibration::targetless
{
	struct Settings
	{
		// How the moving camera's feature tracks are extracted from its footage. Its execution policy is
		// the calibration's: the report records it as run.
		feature::SequenceExtractionRequest extraction;
		// The robust loss on each whitened residual of the bundle adjustment, and where an observation
		// counts as an outlier. There is no noise model: every extracted observation carries its own
		// covariance.
		refinement::RobustLoss loss;
		// Whether the bundle adjustment estimates the principal point. Self-calibration conditions it
		// poorly, so by default it is held where it started: at the image centre, or at an imported
		// model's (CONTEXT.md, "Held parameter").
		bool freePrincipalPoint = false;
	};
} // namespace lain::camera::calibration::targetless
