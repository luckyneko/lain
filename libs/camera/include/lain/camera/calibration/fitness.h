#pragma once

#include "lain/camera/method.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lain::camera::calibration
{
	// The acceptance criteria one fitness tier applies (CONTEXT.md, "Reconstruction fitness"). Every
	// criterion is resolution-independent: angles and fractions, not pixel counts, so one profile
	// serves a VGA webcam and an 8K cinema camera. Raw pixel errors stay in the report for diagnosis.
	struct FitnessThresholds
	{
		std::uint32_t minimumViews = 0;			   // calibration views selected
		double minimumCoverage = 0;				   // fraction of the image grid the calibration views' corners reach
		double maximumHeldOutAngle = 0;			   // radians: RMS angle between observed and predicted rays, held out
		double maximumFocalVariation = 0;		   // relative standard deviation of fx and fy across resamples
		double maximumPrincipalPointVariation = 0; // radians: standard deviation of cx and cy over the focal length
	};

	// Explicit request overrides, applied to the Ready tier. Unset keeps the profile's value.
	struct FitnessOverrides
	{
		std::optional<std::uint32_t> minimumViews;
		std::optional<double> minimumCoverage;
		std::optional<double> maximumHeldOutAngle;
		std::optional<double> maximumFocalVariation;
		std::optional<double> maximumPrincipalPointVariation;
	};

	// A named, VERSIONED pair of tiers ("reconstruction/1"). A published profile's numbers never
	// change: different numbers are a new version, so a report that names a profile says exactly
	// what it was held to. Reports record the resolved thresholds anyway.
	struct FitnessProfile
	{
		std::string name;
		FitnessThresholds ready;
		FitnessThresholds exploratory;
	};

	// The built-in profile of this name, or nullopt for a name this build does not know.
	std::optional<FitnessProfile> fitnessProfile(std::string_view name);
	std::vector<std::string> fitnessProfileNames();

	// The profile with the overrides applied to its Ready tier.
	FitnessProfile resolve(const FitnessProfile& profile, const FitnessOverrides& overrides);

	// What a calibration may be used for (CONTEXT.md, "Reconstruction fitness"): the verdict every
	// camera method module shares (method.h).
	using camera::Verdict;
} // namespace lain::camera::calibration
