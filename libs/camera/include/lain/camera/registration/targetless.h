#pragma once

#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/feature/extraction.h"
#include "lain/camera/feature/tracks.h"
#include "lain/camera/registration/report.h"
#include "lain/camera/registration/request.h"
#include "lain/camera/registration/rig.h"

#include <vector>

// Fixed-camera targetless registration with known intrinsics, the second registration method module
// (ADR-0016, ADR-0017): the cameras placed from the static scene they share, through accepted feature
// tracks. Backend-neutral: features, matches and candidate poses come from the registered backends,
// and the global refinement from the registered refiner; everything that decides what the result
// MEANS (which tracks fit and which validate, which pair seeds the rig, which cameras can be placed at
// all, what an outlier is, how stable the result is, and what it is fit for) is here. Without metric
// evidence the result is scale-ambiguous, and normalised so the median depth of the landmarks the
// reference sees is 1 (CONTEXT.md, "Scale normalisation").
namespace lain::camera::registration::targetless
{
	// Register cameras from footage: feature::extractTracks over `cameras` and `groups` as `extraction`
	// says, then everything registerCameras(tracks) does, exactly. Always a report, successful or
	// failed; its diagnostics keep the extraction's report and track set, so a later comparison can
	// validate on them without extracting again.
	//
	// Every check that needs no frame comes first, so a request that cannot succeed decodes nothing.
	// The registration's execution policy governs the extraction too, and the record holds the
	// extraction request as run. An extraction refused for its dataset is InvalidDataset; for any other
	// reason, ExtractionFailed.
	Report registerCameras(const std::vector<RigFootage>& cameras, const std::vector<capture::CaptureGroup>& groups,
						   const feature::ExtractionRequest& extraction, const Request& request);

	// The same from tracks already accepted. In order:
	// 1. the checks: a profile counting tracks, the cameras, the reference, a well-formed track set,
	//    models that apply, a geometry solver and a refiner;
	// 2. the usable evidence: an observation whose pixel unprojects, a track with two of them;
	// 3. every k-th usable track, in identity order, held out to validate on, unless holding it out
	//    would leave a camera unplaceable from the first seed that places them all;
	// 4. the camera observation graph over the rest, which must be one component;
	// 5. the seed: of the camera pairs whose placement reaches every camera, by how many tracks they
	//    share, the first sixteen are posed; the one with the most inliers whose median triangulation
	//    angle clears 2 degrees seeds the rig, or, when none does, the widest;
	// 6. incremental initialisation in the seed's frame: landmarks triangulated from the placed cameras,
	//    then the unplaced camera seeing the most of them placed by absolute pose, until every camera is;
	// 7. an observation behind its camera, or that it cannot project, left out of the refinement, and
	//    every camera still seeing four landmarks;
	// 8. the global refinement, rebased on the reference and normalised; then outliers named by track
	//    and camera;
	// 9. the held-out tracks predicted member by member (registration::validate);
	// 10. seeded bootstrap resampling of the tracks, each resample's scale aligned, for stability;
	// 11. the verdict against the fitness profile.
	//
	// A rig whose cameras cannot all be placed fails Disconnected, naming the cameras its best seed
	// could not reach (CONTEXT.md, "Placeable camera"). Independent of input order: cameras and tracks
	// are put in identity order first. With ExecutionPolicy::DeterministicDebug the same work runs
	// serially.
	Report registerCameras(const std::vector<RigCamera>& cameras, const feature::TrackSet& tracks, const Request& request);
} // namespace lain::camera::registration::targetless
