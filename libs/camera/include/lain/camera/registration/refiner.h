#pragma once

#include "lain/camera/refinement/refiner.h"
#include "lain/camera/registration/request.h"

// The refiner seam, as registration names it. The seam is lain::camera::refinement's
// (refinement/refiner.h), since calibration refines through it too and neither method may depend on
// the other (ADR-0016); named here as well, where a registration's callers look for it. The names
// the moves changed keep their registration names as aliases, so registration's code, its report and
// its callers read as before. A refinement's summary moved down after the seam did, when calibration
// came to report one too.
namespace lain::camera::registration
{
	using refinement::canRefine;
	using refinement::LandmarkObservation;
	using refinement::Problem;
	using refinement::refine;
	using refinement::Refiner;
	using refinement::refinerRegistry;
	using refinement::Solution;

	using RefinementObservation = refinement::BodyObservation;
	using RefinementStatus = refinement::Status;
	using RefinementSummary = refinement::Summary;
} // namespace lain::camera::registration
