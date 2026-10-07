#include "lain/camera/registration/fitness.h"

namespace lain::camera::registration
{
	// registration/1. Ready asks for what a reconstruction can lean on:
	// - every camera in ten or more capture groups shared with another camera, and every bridge in
	//   the camera graph resting on five or more groups, so no camera hangs from a single view;
	// - held-out transfer residuals within 1 mrad: a pixel at a focal length of 1000, where
	//   calibration's own held-out bound is 0.6 mrad and a transfer adds a second camera's noise;
	// - each camera's rotation steady to 1 mrad across resamples (3 mm at 3 m) and its position to
	//   0.2% of the distance it saw the board from (2 mm at 1 m);
	// - at most 2% of camera-group observations flagged as outliers.
	// Exploratory is the same shape, loose enough to look at but not to build on. Numbers never
	// change under this name.
	static FitnessProfile registration1()
	{
		FitnessProfile p;
		p.name = "registration/1";
		p.unit = EvidenceUnit::CaptureGroup;
		p.ready = {10, 5, 0.001, 0.001, 0.002, 0.02};
		p.exploratory = {4, 2, 0.004, 0.005, 0.01, 0.1};
		return p;
	}

	// registration-targetless/1, counting accepted feature tracks. Provisional (M9 slice 3), re-measured
	// on rendered and real footage before a node exposes it:
	// - every camera in 100 or more shared tracks and every bridge resting on 50, starting from
	//   COLMAP's minimum inliers to initialise and to place a camera (100) and its minimum for
	//   two-view geometry (15), which Exploratory takes;
	// - held-out transfer within 2 mrad: SIFT localises to about 0.7 mrad at a focal length of 1000
	//   processed pixels, and a transfer through a triangulated point roughly doubles that;
	// - rotation steady to 1 mrad and position to 0.5% of the median depth, after the scale each
	//   resample leaves free is aligned;
	// - at most 5% of camera-track observations flagged as outliers: looser than board's, since a
	//   verified match can still err along its epipolar line.
	static FitnessProfile registrationTargetless1()
	{
		FitnessProfile p;
		p.name = "registration-targetless/1";
		p.unit = EvidenceUnit::Track;
		p.ready = {100, 50, 0.002, 0.001, 0.005, 0.05};
		p.exploratory = {30, 15, 0.006, 0.005, 0.02, 0.15};
		return p;
	}

	std::optional<FitnessProfile> fitnessProfile(std::string_view name)
	{
		if (name == "registration/1")
			return registration1();
		if (name == "registration-targetless/1")
			return registrationTargetless1();
		return std::nullopt;
	}

	std::vector<std::string> fitnessProfileNames()
	{
		return {"registration/1", "registration-targetless/1"};
	}

	FitnessProfile resolve(const FitnessProfile& profile, const FitnessOverrides& overrides)
	{
		FitnessProfile out = profile;
		FitnessThresholds& ready = out.ready;
		ready.minimumSharedPerCamera = overrides.minimumSharedPerCamera.value_or(ready.minimumSharedPerCamera);
		ready.minimumBridgeShared = overrides.minimumBridgeShared.value_or(ready.minimumBridgeShared);
		ready.maximumHeldOutAngle = overrides.maximumHeldOutAngle.value_or(ready.maximumHeldOutAngle);
		ready.maximumRotationVariation = overrides.maximumRotationVariation.value_or(ready.maximumRotationVariation);
		ready.maximumTranslationVariation =
			overrides.maximumTranslationVariation.value_or(ready.maximumTranslationVariation);
		ready.maximumOutlierFraction = overrides.maximumOutlierFraction.value_or(ready.maximumOutlierFraction);
		return out;
	}
} // namespace lain::camera::registration
