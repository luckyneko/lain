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

#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
	// member's frame in its footage, shared with feature extraction (footageindex.h), and measures an
	// angle (angles.h); named here too, since inside registration this namespace hides
	// lain::camera::detail.
	using camera::detail::angleBetween;
	using camera::detail::FootageIndex;
	using camera::detail::forEach;

	// The rotation between two transforms, radians: 2 atan2(|v|, |w|) of the relative quaternion,
	// which keeps its precision for the tiny angles stability is measured in, where acos does not.
	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b);

	// The median of `values`; infinity when there are none.
	double median(std::vector<double> values);

	// The root of `x` in a union-find forest, halving the path on the way.
	std::uint32_t root(std::vector<std::uint32_t>& parent, std::uint32_t x);

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

	// Which of `count` units are held out to validate on: every k-th, in order, starting half a stride
	// in, where k is 1/fraction rounded (at least 2), each only when `mayHold(u)` agrees. Nothing is held
	// out with a fraction of zero or fewer than `minimum` units. `mayHold` is asked once per candidate,
	// in order, and may keep state: when it answers true, the unit is held out. A method's guard says
	// what must survive the hold-out: what is held out must not decide whether the registration can
	// happen at all.
	std::vector<bool> holdOut(std::size_t count, double fraction, std::size_t minimum,
							  const std::function<bool(std::size_t)>& mayHold);

	// The same, guarded by connectivity: a unit is kept in when holding it out would cut a camera pair's
	// last shared unit AND so split the camera graph. `unitCameras[u]` lists the cameras that saw unit u.
	std::vector<bool> holdOut(std::size_t cameras, const std::vector<std::vector<std::uint32_t>>& unitCameras,
							  double fraction, std::size_t minimum);

	// The registration reference: `requested` when set, otherwise the camera sharing the most units
	// with others, ties to the lower index.
	std::uint32_t chooseReference(const std::vector<std::uint32_t>& sharedPerCamera, std::optional<std::uint32_t> requested);

	// `resamples` bootstrap draws from `units`, each as many as there are units, with replacement.
	// They come from one engine seeded with `seed`, in order, before anything runs, so they are the same
	// under either execution policy. A plain modulo maps a draw to an index, since the standard's
	// distributions are implementation-defined.
	std::vector<std::vector<std::uint32_t>> bootstrapDraws(const std::vector<std::uint32_t>& units,
														   std::uint32_t resamples, std::uint64_t seed);

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
