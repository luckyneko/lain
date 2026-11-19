#pragma once

#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/method.h"
#include "lain/camera/refinement/refiner.h"
#include "lain/camera/registration/fitness.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lain::camera::registration
{
	// What a camera model whose applicability is Unknown may do in a registration (CONTEXT.md,
	// "Registration camera model"). Incompatible always fails; Unknown is the caller's call, and the
	// report records it per camera either way.
	enum class ApplicabilityPolicy
	{
		Accept, // register with it, and say so in the report
		Refuse, // fail the registration
	};

	// A refinement's noise model and robust loss are the refiner seam's (refinement/refiner.h), named
	// here as well, where a registration request's callers look for them.
	using refinement::LossFamily;
	using refinement::NoiseModel;
	using refinement::RobustLoss;

	// A fixed-camera registration request (CONTEXT.md, "Fixed camera registration"), whatever the
	// method. What a method alone needs (how a board is searched for) is an argument of that method's
	// entry point, and its report records it beside the method's backends (report.h, BoardRecord).
	struct Request
	{
		// The camera given the identity transform. Unset chooses one deterministically: the camera
		// sharing the most evidence with others, ties to the lower identity.
		std::optional<capture::CameraIdentity> reference;
		ApplicabilityPolicy unknownApplicability = ApplicabilityPolicy::Accept;

		NoiseModel noise;
		RobustLoss loss;

		// The fitness profile the verdict is judged by. Empty is the method's own: registration/1 for
		// a board registration, registration-targetless/1 for a targetless one. A method refuses a
		// profile that counts another evidence unit.
		std::string fitnessProfile;
		FitnessOverrides overrides;

		double heldOutFraction = 0.2;		   // of the usable evidence, kept out to validate on
		std::uint32_t resamples = 10;		   // bootstrap re-registrations for the stability evidence; 0 skips it
		std::uint64_t seed = 1;				   // for the resampling draws, recorded in the report
		std::uint32_t maximumIterations = 100; // of each global refinement

		ExecutionPolicy execution = ExecutionPolicy::Normal;
	};
} // namespace lain::camera::registration
