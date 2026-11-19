#pragma once

#include "lain/camera/board/observation.h"
#include "lain/camera/board/specification.h"
#include "lain/camera/calibration/report.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/provenance.h"

#include <lain/core/factory.h>

#include <optional>
#include <string>
#include <vector>

namespace lain::camera::calibration
{
	// What an estimator hands back: UNCHECKED parameters, which the method module passes through
	// CameraModel::create, so a solver's candidate never reaches a report without validation.
	struct Estimate
	{
		std::optional<CameraModelParameters> parameters;
		double rmsPixels = 0; // the backend's own residual over the views it fitted
		std::optional<ParameterUncertainty> uncertainty;
		std::string failure; // why there are no parameters
	};

	// A calibration estimator backend (ADR-0004's service shape). It fits a model to observations;
	// view selection, validation, resampling and the verdict are the method module's, the same
	// whichever backend runs. A board's pose under a fixed model is not an estimator's question but
	// the board module's (board::pose), since registration needs it too.
	class Estimator
	{
	public:
		virtual ~Estimator() = default;

		virtual Provenance provenance() const = 0;

		// Whether this backend can estimate `model`. A variant it cannot estimate is a failed report,
		// never a silent substitution of a similar one (ADR-0016).
		virtual bool canEstimate(DistortionModel model) const = 0;

		// Fit `model` to `views` of `board`, all measured in `image`. With `initial`, start there and
		// refine (the Initial imported-model policy); without, estimate from scratch.
		virtual Estimate estimate(const ImageGeometry& image, const std::vector<camera::board::Observation>& views,
								  const camera::board::Specification& board, DistortionModel model,
								  const std::optional<CameraModelParameters>& initial) const = 0;
	};

	// The process-wide estimator registry, keyed by backend name ("opencv").
	core::Factory<Estimator>& estimatorRegistry();

	// Whether any backend can estimate `model`.
	bool canEstimate(DistortionModel model);
	// Whether any estimator is registered at all.
	bool canEstimate();
} // namespace lain::camera::calibration
