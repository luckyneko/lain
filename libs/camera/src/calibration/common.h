#pragma once

#include "angles.h"
#include "coverage.h"
#include "execution.h"
#include "lain/camera/calibration/report.h"
#include "lain/camera/calibration/request.h"
#include "lain/camera/cameramodel.h"
#include "median.h"
#include "resampling.h"
#include "unionfind.h"
#include "whitening.h"

#include <lain/core/time.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// What every calibration method module decides the same way: private to lain::camera, so board and
// targetless calibration share one answer without either naming the other.
namespace lain::camera::calibration::detail
{
	// How a method module runs independent work (execution.h), counts coverage (coverage.h), holds
	// evidence out and draws bootstrap resamples (resampling.h), measures an angle (angles.h), takes a
	// median (median.h), finds a union-find root (unionfind.h) and whitens a residual (whitening.h),
	// each shared with registration; named here too, since inside calibration this namespace hides
	// lain::camera::detail.
	using camera::detail::angleBetween;
	using camera::detail::bootstrapDraws;
	using camera::detail::cover;
	using camera::detail::CoverageCells;
	using camera::detail::coveredFraction;
	using camera::detail::forEach;
	using camera::detail::holdOut;
	using camera::detail::median;
	using camera::detail::root;
	using camera::detail::whitenedSquared;

	// A report that has failed, timed from `start`.
	Report failed(Report report, core::Time start, Failure failure, std::string detail);

	// The fitness profile `request` names, resolved and with its overrides applied, for a method whose
	// own profile is `ownProfile`: an empty name is the method's own. The request is recorded as given,
	// so a report says whether its profile was asked for or defaulted; the thresholds name what it
	// resolved to. On failure (UnknownFitnessProfile), the report failed.
	std::optional<Report> resolveProfile(Report& report, const Request& request, std::string_view ownProfile, core::Time start);

	// The checks on the imported model against the geometry the evidence is measured in: HoldAndValidate
	// needs one (NoImportedModel), and one that is given must apply (IncompatibleImportedModel). On
	// failure, the report failed.
	std::optional<Report> checkImported(Report& report, const Request& request, const ImageGeometry& image, core::Time start);

	// Where an estimate starts under the Initial policy, and the report's seeded fields: every parameter
	// only when the imported model is the requested model; otherwise the pinhole part alone, with the
	// distortion starting neutral, since coefficients never carry across models on their shape
	// (ADR-0016). Nothing under another policy.
	std::optional<CameraModelParameters> initialFrom(Report& report, const Request& request, const ImageGeometry& image);

	// How much an estimate moves across its resamples (CONTEXT.md, "Reconstruction fitness"): the
	// larger of fx's and fy's relative sample standard deviation, and the larger of cx's and cy's over
	// the mean focal length, or Held when the estimate held the principal point. `estimates` holds one
	// per resample; an unset one produced no model. Unavailable from fewer than two.
	std::variant<ResamplingEvidence, Unavailable> stability(const std::vector<std::optional<CameraModelParameters>>& estimates,
															bool principalPointHeld);
} // namespace lain::camera::calibration::detail
