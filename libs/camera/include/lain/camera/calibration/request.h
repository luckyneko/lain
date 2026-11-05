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
		// for a board calibration, reconstruction-targetless/1 for a targetless one. Either profile may
		// judge either method, since both judge the same criteria. The report records the request
		// exactly as given, and the profile it resolved to in its thresholds.
		std::string fitnessProfile;
		FitnessOverrides overrides;

		// Calibration views selected, at most. Board calibration's alone: a targetless calibration
		// selects no views, since its frames are the extraction's samples.
		std::uint32_t maximumViews = 30;
		double heldOutFraction = 0.2; // of the usable views, or the sampled frames, kept out of estimation to validate on
		std::uint32_t resamples = 20; // bootstrap re-estimations for the stability evidence; 0 skips it
		std::uint64_t seed = 1;		  // for the resampling draws and the geometry's sampling, recorded in the report

		ExecutionPolicy execution = ExecutionPolicy::Normal;
	};
} // namespace lain::camera::calibration
