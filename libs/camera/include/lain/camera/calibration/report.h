#pragma once

#include "lain/camera/board/detection.h"
#include "lain/camera/calibration/fitness.h"
#include "lain/camera/calibration/request.h"
#include "lain/camera/calibration/targetlesssettings.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/feature/sequenceextraction.h"
#include "lain/camera/method.h"
#include "lain/camera/provenance.h"
#include "lain/camera/refinement/refiner.h"

#include <lain/core/time.h>
#include <lain/media/frameref.h>

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
		NoFeatureExtractor,		   // this build cannot extract features
		NoFeatureMatcher,		   // this build cannot match features
		NoGeometrySolver,		   // this build cannot pose frames from rays
		NoRefiner,				   // this build has no bundle adjustment
		UnknownFitnessProfile,	   // the request names a profile this build does not know
		IncompatibleImportedModel, // the imported model's geometry is not the footage's
		NoImportedModel,		   // HoldAndValidate with nothing to hold
		NoFootage,				   // an empty frame sequence, or a track set of no frame
		InvalidDataset,			   // a track set of two cameras, a frame given twice, a track of a frame it lacks
		ExtractionFailed,		   // feature-track extraction refused or failed, not for its dataset
		TooFewViews,			   // fewer usable views than any estimate needs
		InitialisationFailed,	   // no reconstruction could start: no pair of frames could be posed, or none has parallax
		EstimationFailed,		   // the backend could not fit the model
		RefinementFailed,		   // the bundle adjustment produced no usable solution
		ResourceExhausted,		   // the machine ran out of memory
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
		std::uint32_t residuals = 0; // points measured: a board's corners, or a frame's features
		// Views whose pose could not be recovered with the model fixed: a board's pose in board
		// calibration, or in targetless calibration a frame's against the landmarks.
		std::uint32_t viewsWithoutPose = 0;
		double rmsAngle = 0;  // radians, observed against predicted rays, over the residuals
		double rmsPixels = 0; // raw, for diagnosis only
		double worstPixels = 0;
		// Observations not measured because they lie beyond the inlier angle of their view's pose: a
		// wrong match a fundamental matrix could not see, which would otherwise decide the RMS alone.
		// Counted, and named nowhere else. Always 0 in board calibration, whose corners are measured
		// whatever their residual.
		std::uint32_t outliers = 0;
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

	// One start the focal sweep tried (CONTEXT.md, "Focal sweep"): its focal length, the frames it
	// reconstructed, and how its bundle adjustment ended: the final cost per residual, or why it ended in
	// no model. The start that placed the most frames wins, and among those the lowest cost.
	struct SweepStart
	{
		double focalLength = 0;	  // pixels, fx and fy alike
		std::uint32_t placed = 0; // frames placed and kept for the adjustment, or before it failed
		std::variant<double, FailureReason> outcome = 0.0;
	};

	// An observation the result does not explain: its RMS whitened residual exceeds the loss scale,
	// or it could not be projected, from the start or through the result.
	struct Outlier
	{
		std::string track;		 // the feature track's identity
		std::uint32_t frame = 0; // its position among the frames examined
		// Standard deviations; infinite when it was left out of the refinement, or the refined model
		// cannot project it.
		double rmsWhitened = 0;
	};

	// What a targetless calibration examined, beside what every calibration reports. Every frame is
	// named by its position among `frames`. What the reconstruction reports, from `tracksUsable` to
	// `refinement`, is the winning start's; when no start ended in a model, the one that got furthest.
	struct TargetlessDiagnostics
	{
		// When this calibration extracted its tracks: what extraction reported.
		std::optional<feature::SequenceExtractionReport> extraction;
		std::vector<media::FrameRef> frames; // the frames examined, in capture order: one per sampled frame that decoded
		std::uint32_t tracks = 0;			 // given
		std::uint32_t tracksUsable = 0;		 // with two observations outside the held-out frames that unproject
		// Pairs of frames sharing enough tracks to seed, each tested on its pixels for a homography; and
		// of those, the ones a homography explains, which may not seed: their camera turned without
		// moving, or they see one plane.
		std::uint32_t seedPairs = 0;
		std::uint32_t seedPairsRefused = 0;
		std::vector<SweepStart> sweep;		  // in ascending focal length; empty when an imported model started it
		std::optional<std::uint32_t> swept;	  // the start the result came from
		std::vector<std::uint32_t> seed;	  // the two frames the reconstruction started from
		double seedAngle = 0;				  // the median triangulation angle of its inliers that triangulate, radians
		std::uint32_t seedInliers = 0;		  // the matches its relative pose explains
		std::uint32_t seedAtInfinity = 0;	  // of those, the points at infinity: rays meeting at too little parallax
		bool seedBelowFloor = false;		  // no seed cleared the floor, so the widest started it
		std::vector<std::uint32_t> placement; // the frames in the order they were placed, the seed first
		// The frames, not held out, left out of the bundle adjustment: the seed could not reach them, or
		// too few of their landmarks were in front of them. Reported, not fatal.
		std::vector<std::uint32_t> unplaced;
		std::uint32_t behind = 0;		  // observations left out: behind their frame, or unprojectable
		std::uint32_t landmarks = 0;	  // tracks triangulated and refined
		std::uint32_t untriangulated = 0; // usable tracks that never became a refined landmark
		std::optional<refinement::Summary> refinement;
		std::vector<Outlier> outliers; // in capture order of their tracks
	};

	// What every calibration reports about how it got its result, and in `method`, what its method
	// alone examined. A default is a board calibration's, so another method's report sets its own.
	struct Diagnostics
	{
		std::uint32_t viewsSelected = 0;
		std::uint32_t viewsHeldOut = 0;
		double coverage = 0;	 // of the calibration views, over the image grid
		double fitRmsPixels = 0; // the estimate's own residual on the calibration views, over `fitResiduals`
		// The observations fitRmsPixels is taken over: in board calibration every feature of the
		// calibration views; in targetless calibration the fitted observations not named outliers.
		std::uint32_t fitResiduals = 0;
		// Positions among the frames examined (in board calibration, of the detections, one per frame;
		// in targetless calibration, of TargetlessDiagnostics::frames).
		std::vector<std::uint32_t> selected; // the calibration views
		std::vector<std::uint32_t> heldOut;	 // the held-out views
		std::variant<BoardDiagnostics, TargetlessDiagnostics> method;
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

	// What a targetless calibration extracted with, posed by and refined through, beside what every
	// calibration records.
	struct TargetlessRecord
	{
		targetless::Settings settings; // as run: its extraction's execution policy the calibration's
		// When this calibration extracted its tracks: the backends that answered.
		Provenance extractor;
		Provenance matcher;
		Provenance geometry; // what proposed the relative and absolute poses
		Provenance refiner;	 // what ran the bundle adjustment, a held model's included
	};

	// What a report needs to be repeated (CONTEXT.md, "Reproducibility record").
	struct Reproducibility
	{
		std::vector<std::string> sources; // the canonical uri of every source the frames came from
		std::uint32_t frames = 0;
		Provenance estimator; // what estimated the model, whatever kind of backend; unset for a held one
		Request request;	  // the configuration, seed and execution policy, exactly as given
		std::variant<BoardRecord, TargetlessRecord> method;
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
