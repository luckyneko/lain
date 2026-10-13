#pragma once

#include "lain/camera/board/detection.h"
#include "lain/camera/calibration/fitness.h"
#include "lain/camera/calibration/request.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/method.h"
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
		NoPoseSolver,			   // HoldAndValidate in a build that cannot solve board poses
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

	// A section whose evidence could not be computed, and why (method.h).
	using camera::Unavailable;

	// The model checked against views that took no part in estimating it, through lain's own
	// projection (one implementation: what is measured here is what a solver minimises later).
	struct HeldOutEvidence
	{
		std::uint32_t views = 0;
		std::uint32_t residuals = 0; // points measured: a board's corners in board calibration
		// Views whose pose could not be recovered with the model fixed: a board's pose in board
		// calibration, or in targetless calibration a frame's against the landmarks.
		std::uint32_t viewsWithoutPose = 0;
		double rmsAngle = 0;  // radians, observed against predicted rays
		double rmsPixels = 0; // raw, for diagnosis only
		double worstPixels = 0;
	};

	// A parameter the estimate kept at its starting value rather than estimating (CONTEXT.md, "Held
	// parameter"), so there is no resampled variation to measure and its fitness criterion does not
	// apply. Not missing evidence, which Ready refuses, and never a zero, which would read as a
	// parameter measured perfectly stable. A held MODEL is not an estimate at all: its resampling is
	// Unavailable.
	struct Held
	{
	};

	// How much the estimate moves when the calibration views are resampled with replacement.
	struct ResamplingEvidence
	{
		std::uint32_t resamples = 0; // that produced a model
		double focalVariation = 0;	 // relative standard deviation, the larger of fx's and fy's
		// Radians, the larger of cx's and cy's over the mean focal; or Held, when the estimate held the
		// principal point. Board calibration always measures it.
		std::variant<double, Held> principalPointVariation = 0.0;
	};

	// Parameter standard deviations, when the backend computed them. Optional: their cost and meaning
	// vary by backend, and absence is never recorded as zero.
	struct ParameterUncertainty
	{
		double fx = 0, fy = 0, cx = 0, cy = 0;
		std::vector<double> coefficients; // in the distortion model's coefficient order
	};

	// What a board calibration examined, beside what every calibration reports.
	struct BoardDiagnostics
	{
		std::uint32_t framesExamined = 0;
		std::uint32_t framesUsable = 0;							// a board observation with at least the minimum corners
		std::vector<camera::board::DetectionReport> detections; // one per frame, in sequence order
	};

	// What every calibration reports about how it got its result, and in `method`, what its method
	// alone examined. A default is a board calibration's, so another method's report sets its own.
	struct Diagnostics
	{
		std::uint32_t viewsSelected = 0;
		std::uint32_t viewsHeldOut = 0;
		double coverage = 0;	 // of the calibration views, over the image grid
		double fitRmsPixels = 0; // the estimating backend's own residual on the calibration views
		// Positions among the frames examined (in board calibration, of the detections, one per frame).
		std::vector<std::uint32_t> selected; // the calibration views
		std::vector<std::uint32_t> heldOut;	 // the held-out views
		std::variant<BoardDiagnostics> method;
	};

	// What a board calibration detected with, searched with and placed boards by, beside what every
	// calibration records.
	struct BoardRecord
	{
		Provenance detector;
		Provenance poseSolver; // what placed the board in each held-out view
		// What the frames were searched with: the footage overload's argument, or what the first
		// given detection reports. Unset when no detection was given and none was made.
		std::optional<camera::board::DetectionRequest> detection;
	};

	// What a report needs to be repeated (CONTEXT.md, "Reproducibility record").
	struct Reproducibility
	{
		std::vector<std::string> sources; // the canonical uri of every source the frames came from
		std::uint32_t frames = 0;
		Provenance estimator; // what estimated the model, whatever kind of backend; unset for a held one
		Request request;	  // the configuration, seed and execution policy, exactly as given
		std::variant<BoardRecord> method;
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

		// One line for a person: the verdict and the model, then the evidence behind the verdict; or
		// the first reason there is no model.
		std::string toString() const;
	};

	// A verdict and what stood between it and a better one.
	struct Judgement
	{
		Verdict verdict = Verdict::Rejected;
		// Ready's shortfalls, then Exploratory's when that tier fails too: what Report::fitnessNotes
		// holds.
		std::vector<std::string> notes;
	};

	// The verdict `profile` gives a calibration's evidence (CONTEXT.md, "Reconstruction fitness"),
	// whichever method gathered it: `views` are the views supporting the model (the calibration views,
	// or a held model's validating views) and `coverage` theirs. Ready needs every criterion's evidence
	// present, so an Unavailable section refuses it with its reason; Exploratory does not hold missing
	// evidence against it. A Held principal point is not missing evidence: its criterion applies in
	// neither tier.
	Judgement judge(const FitnessProfile& profile, std::uint32_t views, double coverage,
					const std::variant<HeldOutEvidence, Unavailable>& heldOut,
					const std::variant<ResamplingEvidence, Unavailable>& resampling);
} // namespace lain::camera::calibration
