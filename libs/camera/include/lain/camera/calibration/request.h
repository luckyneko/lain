#pragma once

#include "lain/camera/calibration/fitness.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/distortion.h"
#include "lain/camera/method.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lain::camera::calibration
{
	// How an imported model (a manufacturer's, or an external calibration) takes part (CONTEXT.md,
	// "Imported-model policy"). A backend never silently replaces an estimate with imported values.
	enum class ImportedModelPolicy
	{
		Ignore,			 // estimate independently
		Initial,		 // start the estimate from it, then refine
		HoldAndValidate, // keep it exactly, and only validate it against the footage
	};

	// Frames and resamples in parallel on the process pool, or serially in canonical order
	// (method.h).
	using camera::ExecutionPolicy;

	// A calibration request (CONTEXT.md, "Calibration request"): what every calibration method is
	// asked. What only one method uses is an argument of that method's entry point, as board
	// calibration's detection request is calibration::board::calibrate's.
	struct Request
	{
		// The distortion model to estimate, stated explicitly: automatic selection is opt-in and not
		// built yet.
		DistortionModel model = DistortionModel::BrownConrady5;

		std::optional<CameraModel> imported;
		ImportedModelPolicy importedPolicy = ImportedModelPolicy::Ignore;

		// The fitness profile the verdict is judged by. Empty is the method's own: reconstruction/1
		// for a board calibration. The report records the request exactly as given, and the profile
		// it resolved to in its thresholds.
		std::string fitnessProfile;
		FitnessOverrides overrides;

		std::uint32_t maximumViews = 30; // calibration views selected, at most
		double heldOutFraction = 0.2;	 // of the usable views, kept out of estimation to validate on
		std::uint32_t resamples = 20;	 // bootstrap re-estimations for the stability evidence; 0 skips it
		std::uint64_t seed = 1;			 // for the resampling draws, recorded in the report

		ExecutionPolicy execution = ExecutionPolicy::Normal;
	};
} // namespace lain::camera::calibration
