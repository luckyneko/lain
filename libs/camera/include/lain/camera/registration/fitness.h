#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lain::camera::registration
{
	// What a fitness profile counts its evidence in: the capture groups of a board registration, or
	// the accepted feature tracks of a targetless one. A method accepts only a profile of its own unit.
	enum class EvidenceUnit
	{
		CaptureGroup,
		Track,
	};

	// The acceptance criteria one registration-fitness tier applies (CONTEXT.md, "Registration
	// fitness"). Like calibration's, every criterion is independent of image resolution and of the
	// rig's size: angles, fractions, counts of the profile's evidence unit, and a translation relative
	// to how far the cameras saw the scene from. Raw pixel and length figures stay in the report for
	// diagnosis.
	struct FitnessThresholds
	{
		std::uint32_t minimumSharedPerCamera = 0; // units a camera shares with another camera
		std::uint32_t minimumBridgeShared = 0;	  // shared units a bridge edge needs, or it is a weak bridge
		double maximumHeldOutAngle = 0;			  // radians: RMS transfer residual on held-out units
		double maximumRotationVariation = 0;	  // radians: the worst camera's rotation spread across resamples
		double maximumTranslationVariation = 0;	  // the worst camera's translation spread over the median observation depth
		double maximumOutlierFraction = 0;		  // of the camera-unit observations, flagged as outliers
	};

	// Explicit request overrides, applied to the Ready tier. Unset keeps the profile's value.
	struct FitnessOverrides
	{
		std::optional<std::uint32_t> minimumSharedPerCamera;
		std::optional<std::uint32_t> minimumBridgeShared;
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
		EvidenceUnit unit = EvidenceUnit::CaptureGroup; // what its counts count
		FitnessThresholds ready;
		FitnessThresholds exploratory;
	};

	// The built-in profile of this name, or nullopt for a name this build does not know.
	std::optional<FitnessProfile> fitnessProfile(std::string_view name);
	std::vector<std::string> fitnessProfileNames();

	// The profile with the overrides applied to its Ready tier.
	FitnessProfile resolve(const FitnessProfile& profile, const FitnessOverrides& overrides);
} // namespace lain::camera::registration
