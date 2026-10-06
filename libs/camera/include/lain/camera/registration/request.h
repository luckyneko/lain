#pragma once

#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/method.h"
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

	// The pixel noise an observation with no measured covariance is assumed to have (CONTEXT.md,
	// "Observation-noise model"). A detector's response is never turned into this.
	struct NoiseModel
	{
		double pixelSigma = 0.5;
	};

	enum class LossFamily
	{
		None,	// least squares
		Huber,	// quadratic within the scale, linear beyond it
		Cauchy, // logarithmic beyond the scale: an outlier's pull fades
	};

	// The robust loss on each whitened residual, which bounds an outlier's influence. The scale, in
	// standard deviations, is also where an observation counts as an outlier in the report.
	//
	// Cauchy by default, because the outlier a board registration meets is a whole view that does not
	// belong: a frame from another instant, its corners tens of standard deviations off. Huber's pull
	// stays linear out there, and a view of 24 corners still drags the rig: measured on the test rig,
	// one such view at 66 sigma moved a camera 8.4 mrad under Huber and 1.6 under Cauchy, against 1.8
	// with no stray view at all. Cauchy's redescends; its non-convexity is safe because
	// initialisation, which a stray view cannot steer, starts the refinement close.
	struct RobustLoss
	{
		LossFamily family = LossFamily::Cauchy;
		double scale = 3.0;
	};

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

		std::string fitnessProfile = "registration/1";
		FitnessOverrides overrides;

		double heldOutFraction = 0.2;		   // of the usable evidence, kept out to validate on
		std::uint32_t resamples = 10;		   // bootstrap re-registrations for the stability evidence; 0 skips it
		std::uint64_t seed = 1;				   // for the resampling draws, recorded in the report
		std::uint32_t maximumIterations = 100; // of each global refinement

		ExecutionPolicy execution = ExecutionPolicy::Normal;
	};
} // namespace lain::camera::registration
