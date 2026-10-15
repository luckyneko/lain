#include "lain/camera/calibration/fitness.h"

namespace lain::camera::calibration
{
	// reconstruction/1. Ready asks for what a reconstruction can lean on: at least ten views over
	// most of the image, held-out rays within about half a pixel at a typical focal length (0.6 mrad
	// is 0.54 px at f = 900), and an estimate that moves less than half a percent in focal length and
	// about two pixels in principal point when its views are resampled. Exploratory is the same
	// shape, loose enough to look at but not to build on. Numbers never change under this name.
	static FitnessProfile reconstruction1()
	{
		FitnessProfile p;
		p.name = "reconstruction/1";
		p.ready = {10, 0.6, 0.0006, 0.005, 0.002};
		p.exploratory = {6, 0.3, 0.002, 0.02, 0.01};
		return p;
	}

	// reconstruction-targetless/1, targetless calibration's own: reconstruction/1's shape, with the
	// held-out angle loosened in both tiers, since a scene feature is located less precisely than a
	// board corner (SIFT to about 0.7 mrad at f = 1000, M9 slice 3) and a held-out frame adds its own
	// pose's error to every ray. Focal and principal-point stability bound what the model is used for,
	// whichever method found it, so they are board's. Provisional: M9 slice 4's sub-slice 10 measures
	// it on rendered footage before a node exposes it.
	static FitnessProfile reconstructionTargetless1()
	{
		FitnessProfile p;
		p.name = "reconstruction-targetless/1";
		p.ready = {10, 0.6, 0.0015, 0.005, 0.002};
		p.exploratory = {6, 0.3, 0.005, 0.02, 0.01};
		return p;
	}

	std::optional<FitnessProfile> fitnessProfile(std::string_view name)
	{
		if (name == "reconstruction/1")
			return reconstruction1();
		if (name == "reconstruction-targetless/1")
			return reconstructionTargetless1();
		return std::nullopt;
	}

	std::vector<std::string> fitnessProfileNames()
	{
		return {"reconstruction/1", "reconstruction-targetless/1"};
	}

	FitnessProfile resolve(const FitnessProfile& profile, const FitnessOverrides& overrides)
	{
		FitnessProfile out = profile;
		FitnessThresholds& ready = out.ready;
		ready.minimumViews = overrides.minimumViews.value_or(ready.minimumViews);
		ready.minimumCoverage = overrides.minimumCoverage.value_or(ready.minimumCoverage);
		ready.maximumHeldOutAngle = overrides.maximumHeldOutAngle.value_or(ready.maximumHeldOutAngle);
		ready.maximumFocalVariation = overrides.maximumFocalVariation.value_or(ready.maximumFocalVariation);
		ready.maximumPrincipalPointVariation =
			overrides.maximumPrincipalPointVariation.value_or(ready.maximumPrincipalPointVariation);
		return out;
	}
} // namespace lain::camera::calibration
