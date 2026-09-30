#pragma once

#include "lain/camera/board/detection.h"
#include "lain/camera/calibration/fitness.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/distortion.h"

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

	enum class ExecutionPolicy
	{
		Normal,				// frames and resamples in parallel on the process pool
		DeterministicDebug, // the same work, serially, in canonical order
	};

	// A board calibration request (CONTEXT.md, "Calibration request").
	struct Request
	{
		// The distortion model to estimate, stated explicitly: automatic selection is opt-in and not
		// built yet.
		DistortionModel model = DistortionModel::BrownConrady5;

		std::optional<CameraModel> imported;
		ImportedModelPolicy importedPolicy = ImportedModelPolicy::Ignore;

		std::string fitnessProfile = "reconstruction/1";
		FitnessOverrides overrides;

		camera::board::DetectionRequest detection; // how each frame is searched

		std::uint32_t maximumViews = 30; // calibration views selected, at most
		double heldOutFraction = 0.2;	 // of the usable views, kept out of estimation to validate on
		std::uint32_t resamples = 20;	 // bootstrap re-estimations for the stability evidence; 0 skips it
		std::uint64_t seed = 1;			 // for the resampling draws, recorded in the report

		ExecutionPolicy execution = ExecutionPolicy::Normal;
	};
} // namespace lain::camera::calibration
