#pragma once

#include "lain/camera/calibration/report.h"
#include "lain/camera/calibration/request.h"
#include "lain/camera/calibration/targetlesssettings.h"
#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/feature/tracks.h"

// Targetless calibration, the second calibration method module (ADR-0016): one moving camera's model
// estimated from the static scene its footage sees, through accepted feature tracks across its
// sampled frames. Backend-neutral: features, matches and candidate poses come from the registered
// backends, and the bundle adjustment from the registered refiner; everything that decides what the
// result MEANS (where the reconstruction starts, which frames validate, what an outlier is, how stable
// the model is, and what it is fit for) is here, so it is the same whichever backend runs. The
// reconstruction it estimates through has an arbitrary scale and is a diagnostic, never the result.
namespace lain::camera::calibration::targetless
{
	// Calibrate a camera from its footage: feature::extractSequenceTracks over `camera` as the
	// settings' extraction says, then everything calibrate(tracks) does, exactly. Always a report,
	// successful or failed; its diagnostics keep the extraction's report.
	//
	// Every check that needs no frame comes first, in order: the footage (NoFootage), the profile, the
	// imported model, the four backends (an extractor, a matcher, a geometry solver, a refiner), and a
	// refiner that can estimate the requested model unless it is held (NoEstimator). So a request that
	// cannot succeed decodes nothing. The calibration's execution policy governs the extraction too, and
	// the record holds the settings as run. An extraction refused for its dataset is InvalidDataset; for
	// any other reason, ExtractionFailed.
	Report calibrate(const capture::CameraFootage& camera, const Settings& settings, const Request& request);

	// The same from tracks already accepted: one camera's, each view one sampled frame. In order:
	// 1. the checks: a well-formed track set (one camera, at most one frame a view and none twice, one
	//    image geometry, every observation of a view with a frame, each covariance positive definite,
	//    each identity once), the profile, the imported model, a geometry solver, a refiner, and one
	//    that can estimate the requested model unless it is held;
	// 2. the frames in capture order (timestamp, ordinal, source), and the tracks in capture order (by
	//    each one's first frame, then its pixel), so nothing depends on where the footage sits or how
	//    its tracks are named;
	// 3. every k-th frame held out to validate on, unless it seeds the anchor (the first widest seed
	//    over every frame) or holding it out would cut what the anchor reaches;
	// 4. the starting model: an imported one under Initial or HoldAndValidate, otherwise the focal sweep,
	//    21 focal lengths from a 25° to a 120° field of view through the requested model's neutral
	//    projection, each scored by its relative poses' inliers on the eight frame pairs sharing the
	//    most tracks;
	// 5. the reconstruction: the seeds whose placement reaches the most frames, the one with the most
	//    inliers that clears 2 degrees of parallax (or, when none does, the widest), and every frame it
	//    can place by absolute pose. Frames it cannot reach, or that see too few landmarks in front of
	//    them, are reported and left out. A seed most of whose matches are points at infinity has no
	//    parallax, as footage that only turns has none, and fails InitialisationFailed; fewer than three
	//    frames left is TooFewViews;
	// 6. the bundle adjustment: one model shared by every placed frame, its focal length, coefficients
	//    and principal point free unless the settings hold the principal point, under the
	//    settings' robust loss with every observation weighted by its own covariance; then
	//    CameraModel::create, outliers named by track and frame, and the fit RMS over the observations
	//    not named. A held model refines the frames and landmarks alone;
	// 7. each held-out frame posed against the refined landmarks, and its observations within the
	//    pose's inlier angle measured as angles through lain's projection; those beyond it counted;
	// 8. seeded bootstrap resampling of the refined landmarks, for the model's stability: each resample
	//    adjusted from the result, started as the adjustment was but under the refined model, so an
	//    observation the refined model cannot project is left out of it;
	// 9. the verdict against the fitness profile, through calibration::judge.
	//
	// Every inlier test is 4 source pixels turned into an angle by the focal length in use. Independent
	// of the order views and tracks are given in, of their identities, and of the execution policy; with
	// ExecutionPolicy::DeterministicDebug the same work runs serially.
	Report calibrate(const feature::TrackSet& tracks, const Settings& settings, const Request& request);
} // namespace lain::camera::calibration::targetless
