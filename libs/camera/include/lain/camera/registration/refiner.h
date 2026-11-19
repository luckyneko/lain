#pragma once

#include "lain/camera/refinement/refiner.h"
#include "lain/camera/registration/request.h"

// The refiner seam, as registration names it. The seam is lain::camera::refinement's
// (refinement/refiner.h), since calibration refines through it too and neither method may depend on
// the other (ADR-0016); named here as well, where a registration's callers look for it. The names the
// move changed (refinement::Status, ::BodyObservation and ::Summary) are spelled as the seam spells
// them; the registration aliases they kept through the move were retired on 2026-11-13.
namespace lain::camera::registration
{
	using refinement::canRefine;
	using refinement::LandmarkObservation;
	using refinement::Problem;
	using refinement::refine;
	using refinement::Refiner;
	using refinement::refinerRegistry;
	using refinement::Solution;
} // namespace lain::camera::registration
