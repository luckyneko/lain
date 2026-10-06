#pragma once

#include "execution.h"
#include "lain/camera/registration/fitness.h"
#include "lain/camera/registration/observationgraph.h"
#include "lain/camera/registration/report.h"

#include <lain/core/time.h>
#include <lain/math/rigidtransform.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// What every registration method module decides the same way: private to lain::camera, so board and
// targetless registration share one answer without either naming the other. Each method has its own
// evidence unit (a capture group for board registration, a feature track for targetless); these take
// units as numbered lists of the cameras that saw them.
namespace lain::camera::registration::detail
{
	// How a method module runs independent work, shared with calibration (execution.h); named here
	// too, since inside registration this namespace hides lain::camera::detail.
	using camera::detail::forEach;

	// The rotation between two transforms, radians: 2 atan2(|v|, |w|) of the relative quaternion,
	// which keeps its precision for the tiny angles stability is measured in, where acos does not.
	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b);

	// The median of `values`; infinity when there are none.
	double median(std::vector<double> values);

	// The root of `x` in a union-find forest, halving the path on the way.
	std::uint32_t root(std::vector<std::uint32_t>& parent, std::uint32_t x);

	// A report that has failed, timed from `start`.
	Report failed(Report report, core::Time start, Failure failure, std::string detail);

	// Which units are held out to validate on: every k-th, in the order given, starting half a stride
	// in, where k is 1/fraction rounded (at least 2). A unit is kept in, though, when holding it out
	// would cut a camera pair's last shared unit AND so split the camera graph: what is held out must
	// not decide whether the registration can happen at all. Nothing is held out with a fraction of
	// zero or fewer than `minimum` units. `unitCameras[u]` lists the cameras that saw unit u.
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
