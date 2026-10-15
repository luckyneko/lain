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

#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What every registration method module decides the same way: private to lain::camera, so board and
// targetless registration share one answer without either naming the other. Each method has its own
// evidence unit (a capture group for board registration, a feature track for targetless); these take
// units as numbered lists of the cameras that saw them.
namespace lain::camera::registration::detail
{
	// How a method module runs independent work, shared with calibration (execution.h), finds a
	// member's frame in its footage, shared with feature extraction (footageindex.h), measures an angle
	// (angles.h), takes a median (median.h), finds a union-find root (unionfind.h), and holds evidence
	// out and draws bootstrap resamples, shared with calibration (resampling.h); named here too, since
	// inside registration this namespace hides lain::camera::detail.
	using camera::detail::angleBetween;
	using camera::detail::bootstrapDraws;
	using camera::detail::FootageIndex;
	using camera::detail::forEach;
	using camera::detail::holdOut;
	using camera::detail::median;
	using camera::detail::root;

	// The rotation between two transforms, radians: 2 atan2(|v|, |w|) of the relative quaternion,
	// which keeps its precision for the tiny angles stability is measured in, where acos does not.
	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b);

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

	// The squared length of the pixel residual `r` whitened: by the Cholesky factor of `covariance`
	// (xx, xy, yy) when there is one, L^-1 r, else by the noise model's sigma.
	double whitenedSquared(const math::Vec2d& r, const std::optional<std::array<double, 3>>& covariance,
						   const NoiseModel& noise);

	// The registration reference: `requested` when set, otherwise the camera sharing the most units
	// with others, ties to the lower index.
	std::uint32_t chooseReference(const std::vector<std::uint32_t>& sharedPerCamera, std::optional<std::uint32_t> requested);

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
