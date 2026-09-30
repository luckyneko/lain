#pragma once

#include "lain/camera/board/detection.h"
#include "lain/camera/calibration/fitness.h"
#include "lain/camera/calibration/request.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/provenance.h"

#include <lain/core/time.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lain::camera::calibration
{
	enum class CalibrationStatus
	{
		Succeeded, // a validated model exists; the verdict says what it is fit for
		Failed,	   // no model
	};

	enum class Failure
	{
		NoDetector,				   // this build cannot detect boards
		NoEstimator,			   // this build cannot estimate the requested model
		UnknownFitnessProfile,	   // the request names a profile this build does not know
		IncompatibleImportedModel, // the imported model's geometry is not the footage's
		NoImportedModel,		   // HoldAndValidate with nothing to hold
		NoFootage,				   // an empty frame sequence
		TooFewViews,			   // fewer usable views than any estimate needs
		EstimationFailed,		   // the backend could not fit the model
		InvalidModel,			   // the backend's result is not a valid camera model
	};

	struct FailureReason
	{
		Failure failure;
		std::string detail;
	};

	// A section whose evidence could not be computed, and why. Never zeros: missing evidence is not
	// perfect evidence.
	struct Unavailable
	{
		std::string reason;
	};

	// The model checked against views that took no part in estimating it, through lain's own
	// projection (one implementation: what is measured here is what a solver minimises later).
	struct HeldOutEvidence
	{
		std::uint32_t views = 0;
		std::uint32_t corners = 0;
		std::uint32_t viewsWithoutPose = 0; // a board pose could not be recovered with the model fixed
		double rmsAngle = 0;				// radians, observed against predicted rays
		double rmsPixels = 0;				// raw, for diagnosis only
		double worstPixels = 0;
	};

	// How much the estimate moves when the calibration views are resampled with replacement.
	struct ResamplingEvidence
	{
		std::uint32_t resamples = 0;		// that produced a model
		double focalVariation = 0;			// relative standard deviation, the larger of fx's and fy's
		double principalPointVariation = 0; // radians, the larger of cx's and cy's over the mean focal
	};

	// Parameter standard deviations, when the backend computed them. Optional: their cost and meaning
	// vary by backend, and absence is never recorded as zero.
	struct ParameterUncertainty
	{
		double fx = 0, fy = 0, cx = 0, cy = 0;
		std::vector<double> coefficients; // in the distortion model's coefficient order
	};

	struct Diagnostics
	{
		std::uint32_t framesExamined = 0;
		std::uint32_t framesUsable = 0; // a board observation with at least the minimum corners
		std::uint32_t viewsSelected = 0;
		std::uint32_t viewsHeldOut = 0;
		double coverage = 0;									// of the calibration views, over the image grid
		double fitRmsPixels = 0;								// the backend's own residual on the calibration views
		std::vector<camera::board::DetectionReport> detections; // one per frame, in sequence order
		std::vector<std::uint32_t> selected;					// positions of the calibration views
		std::vector<std::uint32_t> heldOut;						// positions of the held-out views
	};

	// What a report needs to be repeated (CONTEXT.md, "Reproducibility record").
	struct Reproducibility
	{
		std::vector<std::string> sources; // the canonical uri of every source the frames came from
		std::uint32_t frames = 0;
		Provenance detector;
		Provenance estimator;
		Request request; // the configuration, seed and execution policy, exactly as given
	};

	// The result of a calibration attempt, successful or not (CONTEXT.md, "Calibration report").
	struct Report
	{
		CalibrationStatus status = CalibrationStatus::Failed;
		std::vector<FailureReason> failures;
		std::optional<CameraModel> model;

		Verdict verdict = Verdict::Rejected;
		std::vector<std::string> fitnessNotes; // each criterion a better verdict would have needed
		FitnessProfile thresholds;			   // fully resolved, overrides applied

		std::variant<HeldOutEvidence, Unavailable> heldOut = Unavailable{"not computed"};
		std::variant<ResamplingEvidence, Unavailable> resampling = Unavailable{"not computed"};
		std::optional<ParameterUncertainty> uncertainty;

		// Which fields of an imported model seeded the estimate ("fx", "k1", ...), when one did.
		std::vector<std::string> seededFields;

		Diagnostics diagnostics;
		Reproducibility reproducibility;
		core::Time elapsed;
	};
} // namespace lain::camera::calibration
