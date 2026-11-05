#pragma once

#include "lain/camera/feature/tracks.h"
#include "lain/camera/method.h"
#include "lain/camera/registration/report.h"
#include "lain/camera/registration/rig.h"

#include <string>
#include <variant>
#include <vector>

namespace lain::camera::registration
{
	// A registration checked against feature tracks that took no part in it (CONTEXT.md, "Held-out
	// track"): each member of each track in `trackIds` predicted from the others, with every camera held
	// where `report` registered it. Whichever method made the report, so board and targetless
	// registrations are compared on the same tracks (ADR-0016). Angles only, so it is scale-invariant:
	// a metric registration and a scale-normalised one are compared without fitting anything.
	//
	// The procedure is fixed, whatever the report's request: each observation is weighted by its own
	// covariance (NoiseModel{} when it has none), with no robust loss. A report is judged by what it
	// registered, never by how it asked to be refined.
	//
	// A member is predicted when its camera is registered in `report`, has a model in `cameras`, and
	// its pixel unprojects:
	// - in a track of three or more such members, its landmark is triangulated from the others and
	//   refined with every camera held, one refinement per track, so a track's prediction does not
	//   depend on which other tracks were held out. A start that does not triangulate in front of the
	//   others, or does not project into each of them, leaves the member unpredicted. Its transfer
	//   residual is the angle between its observed ray and the ray to the predicted point;
	// - in a track of two, the other member alone fixes no point, so the member is scored by the angle
	//   between its observed ray and the epipolar plane through both camera centres and the other's
	//   ray. Unpredicted when the plane is undefined (no baseline, or the other ray along it) or the
	//   pair meets behind a camera, which this residual alone could not see. Such a residual has one
	//   degree of freedom where a transfer residual has two, so it counts twice in the RMS angles,
	//   which keeps them independent of the mix of track lengths under isotropic noise
	//   (HeldOutEvidence::epipolar counts them).
	//
	// Unavailable when the report failed, no track id is given, an id names no track of `trackSet`
	// (the first such id is named), this build has no refiner, or no member could be predicted. Ids
	// given twice count once; the order they are given in changes nothing. With
	// ExecutionPolicy::DeterministicDebug the same work runs serially.
	std::variant<HeldOutEvidence, Unavailable> validate(const std::vector<RigCamera>& cameras, const Report& report,
														const feature::TrackSet& trackSet,
														const std::vector<std::string>& trackIds,
														ExecutionPolicy execution = ExecutionPolicy::Normal);
} // namespace lain::camera::registration
