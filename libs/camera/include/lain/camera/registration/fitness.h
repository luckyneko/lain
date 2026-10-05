#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lain::camera::registration
{
	// The acceptance criteria one registration-fitness tier applies (CONTEXT.md, "Registration
	// fitness"). Like calibration's, every criterion is independent of image resolution and of the
	// rig's size: angles, fractions, counts, and a translation relative to how far the cameras saw
	// the board from. Raw pixel and metre figures stay in the report for diagnosis.
	struct FitnessThresholds
	{
		std::uint32_t minimumGroupsPerCamera = 0; // capture groups in which a camera saw the board with another camera
		std::uint32_t minimumBridgeGroups = 0;	  // shared groups a bridge edge needs, or it is a weak bridge
		double maximumHeldOutAngle = 0;			  // radians: RMS transfer residual on held-out groups
		double maximumRotationVariation = 0;	  // radians: the worst camera's rotation spread across resamples
		double maximumTranslationVariation = 0;	  // the worst camera's translation spread over the median observation depth
		double maximumOutlierFraction = 0;		  // of the camera-group observations, flagged as outliers
	};

	// Explicit request overrides, applied to the Ready tier. Unset keeps the profile's value.
	struct FitnessOverrides
	{
		std::optional<std::uint32_t> minimumGroupsPerCamera;
		std::optional<std::uint32_t> minimumBridgeGroups;
		std::optional<double> maximumHeldOutAngle;
		std::optional<double> maximumRotationVariation;
		std::optional<double> maximumTranslationVariation;
		std::optional<double> maximumOutlierFraction;
	};

	// A named, VERSIONED pair of tiers ("registration/1"). A published profile's numbers never
	// change: different numbers are a new version. Reports record the resolved thresholds anyway.
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
} // namespace lain::camera::registration
