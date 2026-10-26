#pragma once

#include "angles.h"
#include "execution.h"
#include "footageindex.h"
#include "lain/camera/cameramodel.h"
#include "lain/camera/registration/fitness.h"
#include "lain/camera/registration/observationgraph.h"
#include "lain/camera/registration/report.h"
#include "lain/camera/registration/request.h"
#include "lain/camera/registration/rig.h"
#include "median.h"
#include "resampling.h"
#include "unionfind.h"
#include "whitening.h"

#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// What every registration method module decides the same way: private to lain::camera, so board and
// targetless registration share one answer without either naming the other. Each method has its own
// evidence unit (a capture group for board registration, a feature track for targetless); these take
// units as numbered lists of the cameras that saw them.
namespace lain::camera::registration::detail
{
	// How a method module runs independent work, shared with calibration (execution.h), finds a
	// member's frame in its footage, shared with feature extraction (footageindex.h), measures an angle
	// or a rotation (angles.h), takes a median (median.h), finds a union-find root (unionfind.h), and
	// holds evidence out and draws bootstrap resamples, shared with calibration (resampling.h); named
	// here too, since inside registration this namespace hides lain::camera::detail. A residual is
	// whitened as every method whitens one (whitening.h).
	using camera::detail::angleBetween;
	using camera::detail::bootstrapDraws;
	using camera::detail::FootageIndex;
	using camera::detail::forEach;
	using camera::detail::holdOut;
	using camera::detail::median;
	using camera::detail::root;
	using camera::detail::rotationBetween;
	using camera::detail::whitenedSquared;

	// How a person reads a count of `unit`s, plural: "groups", "tracks".
	std::string_view unitsWord(EvidenceUnit unit);

	// A report that has failed, timed from `start`.
	Report failed(Report report, core::Time start, Failure failure, std::string detail);

	// The fitness profile `request` names, resolved and with its overrides applied, for a method whose
	// evidence unit is `unit` and whose own profile is `ownProfile`: an empty name is the method's own.
	// On failure (UnknownFitnessProfile, IncompatibleFitnessProfile), the report failed.
	std::optional<Report> resolveProfile(Report& report, const Request& request, EvidenceUnit unit,
										 std::string_view ownProfile, core::Time start);

	// A rig's cameras in identity order, so nothing downstream depends on how they were listed.
	struct CanonicalCameras
	{
		std::vector<RigCamera> cameras;			// ascending by identity
		std::vector<std::size_t> given;			// per canonical camera: its index in the list given
		std::optional<std::uint32_t> reference; // the requested reference's index, when one was requested

		// The index of `camera`, or nullopt when the rig has no such camera.
		std::optional<std::uint32_t> indexOf(const capture::CameraIdentity& camera) const;
	};

	// The checks on the cameras alone: an identity each, none twice (InvalidDataset), at least two
	// (TooFewCameras), and a requested reference among them (UnknownReference). On failure, the report
	// failed.
	std::optional<Report> canonicalCameras(Report& report, CanonicalCameras& out, const std::vector<RigCamera>& cameras,
										   const Request& request, core::Time start);

	// Each canonical camera's model against the geometry its evidence is measured in (`geometry`, per
	// canonical camera; unset is its model's own): Incompatible fails (IncompatibleModel), Unknown fails
	// when the request refuses it (UnknownApplicability). On failure, the report failed.
	std::optional<Report> checkApplicability(Report& report, std::vector<Applicability>& out,
											 const std::vector<RigCamera>& cameras,
											 const std::vector<std::optional<ImageGeometry>>& geometry,
											 const Request& request, core::Time start);

	// The registration reference: `requested` when set, otherwise the camera sharing the most units
	// with others, ties to the lower index.
	std::uint32_t chooseReference(const std::vector<std::uint32_t>& sharedPerCamera, std::optional<std::uint32_t> requested);

	// What a held-out member's residual is, which decides how its median is read.
	enum class HeldOutKind
	{
		Transfer, // a track member against its point predicted from the others: a 2-D error
		Epipolar, // a two-member track's member against the other's epipolar plane: a 1-D error
		View,	  // a board view against its board posed from the others: 2-D errors over its corners
	};

	// One held-out member's score, as a method produces it: one camera's view of a held-out unit,
	// predicted from the unit's other members. Board registration and registration::validate each
	// score their own members and gather them through heldOutEvidence, so held-out evidence is counted
	// and judged one way.
	struct HeldOutMember
	{
		std::uint32_t camera = 0; // into the cameras heldOutEvidence is given
		HeldOutKind kind = HeldOutKind::Transfer;
		std::uint32_t residuals = 0;   // its points: a board view's corners, or one for a track's member
		double sumSquaredAngle = 0;	   // radians squared over its points, weighted (an epipolar residual twice)
		std::uint32_t pixelPoints = 0; // its points with a predicted pixel
		double sumSquaredPixels = 0;   // pixels squared over those
		double worstPixels = 0;
	};

	// The held-out evidence of `units` held-out units from their PREDICTED members and the count of
	// members that could not be predicted: the RMS angles, and the median-based ones fitness judges
	// (HeldOutEvidence). Unavailable when nothing was predicted.
	std::variant<HeldOutEvidence, Unavailable> heldOutEvidence(std::uint32_t units, std::uint32_t unpredicted,
															   const std::vector<HeldOutMember>& members,
															   const std::vector<capture::CameraIdentity>& cameras);

	// A camera's stability across resamples, from the distance of each resample's camera to the result's
	// (a rotation angle, or a translation): their median, scaled so that for resamples scattered normally
	// about the result it equals their RMS distance, and the profiles' bounds keep their meaning. The
	// scale is sqrt(3) over the median of a chi distribution of three degrees of freedom, since a
	// rotation's and a translation's distances are each the length of a 3-vector. A median, so one
	// resample that left the rig poorly conditioned cannot decide the verdict, as calibration's stability
	// became robust on 2026-10-19. Infinite with no distances.
	double resampledSpread(std::vector<double> distances);

	// Whether the evidence meets one tier. `complete` demands every criterion's evidence be present
	// (Ready); otherwise a criterion whose evidence is unavailable is not held against it. Notes say
	// what fell short.
	bool meets(const FitnessThresholds& t, const char* tier, bool complete, const Report& report,
			   const ObservationGraph& graph, std::uint32_t outliers, std::uint32_t fitted, std::vector<std::string>& notes);

	// The verdict: Ready if every Ready criterion is met with all its evidence, else Exploratory if the
	// Exploratory tier is met with what evidence there is, else Rejected. The notes of both tiers say
	// what fell short.
	void judge(Report& report, const ObservationGraph& graph, std::uint32_t outliers, std::uint32_t fitted);
} // namespace lain::camera::registration::detail
