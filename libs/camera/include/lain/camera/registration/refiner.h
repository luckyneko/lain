#pragma once

#include "lain/camera/refinement/refiner.h"
#include "lain/camera/registration/request.h"

// The refiner seam, as registration names it. The seam is lain::camera::refinement's
// (refinement/refiner.h), since calibration refines through it too and neither method may depend on
// the other (ADR-0016); named here as well, where a registration's callers look for it. The two
// renamed in the move keep their registration names as aliases, so registration's code, its report
// and its callers read as before.
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
} // namespace lain::camera::registration
