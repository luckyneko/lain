#pragma once

#include "lain/camera/board/detection.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/method.h"
#include "lain/camera/provenance.h"
#include "lain/camera/registration/fitness.h"
#include "lain/camera/registration/refiner.h"
#include "lain/camera/registration/request.h"

#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lain::camera::registration
{
	enum class RegistrationStatus
	{
		Succeeded, // every camera has a registered transform; the verdict says what it is fit for
		Failed,	   // none is authoritative
	};

	enum class Failure
	{
		NoDetector,			   // this build cannot detect boards
		NoPoseSolver,		   // this build cannot solve board poses
		NoRefiner,			   // this build has no global refinement
		UnknownFitnessProfile, // the request names a profile this build does not know
		InvalidDataset,		   // two cameras with one identity, a member of no camera, a frame its footage lacks
		IncompatibleModel,	   // a camera model's geometry is not its footage's
		UnknownApplicability,  // a model's applicability is Unknown and the request refuses that
		UnknownReference,	   // the requested reference camera is not in the dataset
		TooFewCameras,		   // fewer than two cameras saw the board together
		Disconnected,		   // the camera graph falls apart into components
		InitialisationFailed,  // a relative pose could not be estimated
		RefinementFailed,	   // the global refinement produced no usable solution
		ResourceExhausted,	   // the machine ran out of memory
	};

	struct FailureReason
	{
		Failure failure;
		std::string detail;
	};

	// Whether the registered translations are in metres, or share one arbitrary scale (CONTEXT.md,
	// "Registration scale status"). Board registration is metric: the board's measurement is the
	// evidence, and `evidence` names it.
	enum class Scale
	{
		Metric,
		Arbitrary,
	};

	struct ScaleStatus
	{
		Scale scale = Scale::Arbitrary;
		std::string evidence;
	};

	// One camera's registered transform (CONTEXT.md, "Registered camera transform"): referenceFromCamera
	// only, its inverse derived where needed.
	struct RegisteredCamera
	{
		capture::CameraIdentity camera;
		math::RigidTransformd referenceFromCamera;
	};

	// What each camera brought to the registration, and how well the result explains it.
	struct CameraEvidence
	{
		capture::CameraIdentity camera;
		Applicability applicability = Applicability::Unknown;
		std::uint32_t groups = 0;	// capture groups it saw the board in with another camera
		std::uint32_t corners = 0;	// corners it contributed to the refinement
		std::uint32_t outliers = 0; // of its observations
		double rmsPixels = 0;		// fitted residuals, raw, for diagnosis
		double rmsAngle = 0;		// radians
	};

	struct GraphEdgeReport
	{
		capture::CameraIdentity a;
		capture::CameraIdentity b;
		std::uint32_t sharedGroups = 0;
		bool bridge = false;
	};

	// One connected component's cameras, placed relative to its own first camera. Only for a
	// disconnected dataset, and only as a diagnostic: a registration is the whole rig or nothing.
	struct ComponentEstimate
	{
		capture::CameraIdentity reference;
		std::vector<RegisteredCamera> cameras;
	};

	// An observation whose backend-ranked pose was the wrong one of a planar target's two: the
	// other cameras of its group agreed with the alternative (CONTEXT.md, "Pose solver").
	struct FlipChoice
	{
		std::string group;
		capture::CameraIdentity camera;
	};

	// An observation the result does not explain: its RMS whitened residual exceeds the loss scale.
	struct Outlier
	{
		std::string group;
		capture::CameraIdentity camera;
		double rmsWhitened = 0; // standard deviations
	};

	struct RefinementSummary
	{
		RefinementStatus status = RefinementStatus::Failed;
		std::string detail;
		std::uint32_t iterations = 0;
		double initialCost = 0;
		double finalCost = 0;
		std::uint32_t corners = 0; // residual blocks: one corner seen by one camera
		std::uint32_t bodies = 0;  // board poses estimated alongside the cameras
		core::Time elapsed;
		NoiseModel noise;
		RobustLoss loss;
	};

	// The registration checked against capture groups that took no part in it: each member of a
	// held-out group predicted from the others (CONTEXT.md, "Held-out group").
	struct HeldOutEvidence
	{
		std::uint32_t groups = 0;
		std::uint32_t predictions = 0; // members predicted
		std::uint32_t unpredicted = 0; // members whose board could not be placed from the others
		std::uint32_t corners = 0;
		double rmsAngle = 0;  // radians: the transfer residual
		double rmsPixels = 0; // raw, for diagnosis only
		double worstPixels = 0;
		std::vector<std::pair<capture::CameraIdentity, double>> perCamera; // RMS transfer angle
	};

	// How much the registration moves when its capture groups are resampled with replacement.
	struct ResamplingEvidence
	{
		std::uint32_t resamples = 0;		   // that produced a registration
		double rotationVariation = 0;		   // radians: the worst camera's RMS rotation from the result
		double translationVariation = 0;	   // the worst camera's RMS translation over the median observation depth
		double translationVariationMetres = 0; // the same, in metres, for diagnosis
		capture::CameraIdentity worstCamera;
	};

	// One capture group's detections, for diagnosis and debug views.
	struct GroupDetections
	{
		std::string group;
		std::vector<std::pair<capture::CameraIdentity, camera::board::DetectionReport>> members;
	};

	struct Diagnostics
	{
		std::uint32_t groupsExamined = 0;
		std::uint32_t groupsUsable = 0;			   // with a posed board in two or more cameras
		std::uint32_t observations = 0;			   // camera-group observations with a usable detection
		std::uint32_t observationsWithoutPose = 0; // of those, no board pose could be solved
		std::vector<GraphEdgeReport> edges;
		std::vector<std::vector<capture::CameraIdentity>> components;
		std::vector<GraphEdgeReport> weakBridges; // against the Ready tier's minimumBridgeGroups
		std::vector<ComponentEstimate> componentEstimates;
		std::vector<FlipChoice> flips;
		std::vector<Outlier> outliers;
		std::optional<RefinementSummary> refinement;
		std::vector<CameraEvidence> cameras; // ascending by identity
		std::vector<std::string> heldOutGroups;
		double medianDepth = 0;					 // metres: what translation variation is relative to
		std::vector<GroupDetections> detections; // in canonical group order
	};

	// What a board registration searched with and solved by, beside what every registration records.
	struct BoardRecord
	{
		Provenance detector;
		Provenance poseSolver;
		// What the frames were searched with: the footage overload's argument, or what the first
		// given detection reports. Unset when no detection was given and none was made.
		std::optional<camera::board::DetectionRequest> detection;
	};

	// What a report needs to be repeated (CONTEXT.md, "Reproducibility record"): what every
	// registration records, and in `method`, what its method alone does.
	struct Reproducibility
	{
		std::vector<std::string> sources; // the canonical uri of every source the frames came from
		std::uint32_t frames = 0;
		Provenance refiner;
		Request request; // exactly as given
		std::variant<BoardRecord> method;
	};

	// The result of a fixed-camera registration, successful or not (CONTEXT.md, "Registration report").
	struct Report
	{
		RegistrationStatus status = RegistrationStatus::Failed;
		std::vector<FailureReason> failures;

		std::optional<capture::CameraIdentity> reference;
		bool referenceRequested = false;	   // chosen by the request, rather than automatically
		std::vector<RegisteredCamera> cameras; // ascending by identity; empty unless Succeeded
		ScaleStatus scale;

		Verdict verdict = Verdict::Rejected;
		std::vector<std::string> fitnessNotes; // each criterion a better verdict would have needed
		FitnessProfile thresholds;			   // fully resolved, overrides applied

		std::variant<HeldOutEvidence, Unavailable> heldOut = Unavailable{"not computed"};
		std::variant<ResamplingEvidence, Unavailable> resampling = Unavailable{"not computed"};

		Diagnostics diagnostics;
		Reproducibility reproducibility;
		core::Time elapsed;

		// `camera`'s registered transform, or nullopt when it has none.
		std::optional<math::RigidTransformd> referenceFromCamera(const capture::CameraIdentity& camera) const;

		// One line for a person: the verdict, the cameras and the evidence; or the first reason
		// there is no registration.
		std::string toString() const;
	};
} // namespace lain::camera::registration
