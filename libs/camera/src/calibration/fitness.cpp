#include "lain/camera/calibration/fitness.h"

#include <limits>

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

	// reconstruction-targetless/1, targetless calibration's own: reconstruction/1's shape, its held-out
	// criterion the residuals whitened by each feature's stated covariance instead of an angle. A
	// feature is located to a fraction of a pixel whatever the lens: measured on rendered footage (M9
	// slice 4, sub-slice 10e and after), the held-out residual was 0.6 to 0.9 px at focal lengths of
	// 250 to 1500, so the angle went from 0.6 to 2.2 mrad and a wide lens's accurate model failed a
	// fixed one. The whitened RMS of the right models was 1.2 to 2.9 per axis over every lens and
	// texture tried, since small features state less than their true error; 4 bounds Ready, 8
	// Exploratory. Neither statistic sees a wrong model whose error the reconstruction absorbs: a
	// principal point held 10 px off, or no distortion model on a lens that has one, validated as
	// well as the truth. Focal and principal-point stability bound what the model is used for,
	// whichever method found it, so they are board's.
	static FitnessProfile reconstructionTargetless1()
	{
		constexpr double unjudged = std::numeric_limits<double>::infinity();
		FitnessProfile p;
		p.name = "reconstruction-targetless/1";
		p.ready = {10, 0.6, unjudged, 0.005, 0.002, 4.0};
		p.exploratory = {6, 0.3, unjudged, 0.02, 0.01, 8.0};
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
		ready.maximumHeldOutWhitened = overrides.maximumHeldOutWhitened.value_or(ready.maximumHeldOutWhitened);
		return out;
	}
} // namespace lain::camera::calibration
