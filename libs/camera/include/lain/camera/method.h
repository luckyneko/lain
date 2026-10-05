#pragma once

#include <string>

// What the camera METHOD modules share — calibration (calibration::board) and registration
// (registration::board), and the targetless methods after them (ADR-0016) — so neither has to name
// the other to speak about its result.
namespace lain::camera
{
	// What a result may be used for (CONTEXT.md, "Reconstruction fitness", "Registration fitness").
	enum class Verdict
	{
		Rejected,
		Exploratory,
		Ready,
	};

	// A section of a report whose evidence could not be computed, and why. Never zeros: missing
	// evidence is not perfect evidence.
	struct Unavailable
	{
		std::string reason;
	};

	enum class ExecutionPolicy
	{
		Normal,				// independent work in parallel on the process pool
		DeterministicDebug, // the same work, serially, in canonical order
	};
} // namespace lain::camera
