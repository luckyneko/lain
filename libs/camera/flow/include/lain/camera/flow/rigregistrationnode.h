#pragma once

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/registration/report.h>
#include <lain/camera/registration/request.h>
#include <lain/camera/rig.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

#include <string>
#include <variant>
#include <vector>

namespace lain::camera
{
	// What the fixed-camera registration nodes share, whatever their method (registerCameras,
	// registerCamerasTargetless): a rig's `footage` and `models`, collections paired by position so one
	// wire carries every camera and the inputs compose with a map (ADR-0016, amended); the
	// registration::Request settings every method takes; and the rig they make. Frame k of every
	// camera is capture group k (by-position grouping), which is right for a frame-locked rig and only
	// for one.
	//
	// Each camera's identity is its footage's canonical source uri, a stand-in until a capture manifest
	// assigns identities. `reference` names one by that uri; empty chooses automatically.
	//
	// Abstract: a method's node declares its own inputs after the two here, calls declareRequest(),
	// declares what only its method needs and its report, and builds the report from request() and
	// rig(). One routine for the pairing and one for the settings, so the two methods cannot come to
	// mean different things by the same name.
	class RigRegistrationNode : public flow::Node
	{
	protected:
		// Declares `footage` and `models`, inputs 0 and 1, so a method's own inputs follow them.
		explicit RigRegistrationNode(std::string name);

		// Declare the settings every registration takes, with registration::Request's defaults:
		// reference, unknownApplicability, loss, lossScale, fitnessProfile, heldOutFraction,
		// resamples, seed, maximumIterations and deterministic. Not the noise model: whether one
		// applies depends on whether a method's observations carry their own covariance, so a method
		// whose observations may not declares `pixelSigma` itself.
		void declareRequest();

		// The request those settings ask for, its noise model the default. A float is the decimal a
		// person typed, a count below 0 is 0, and a seed keeps its bits.
		registration::Request request() const;

		struct Rig
		{
			std::vector<RigFootage> cameras;
			std::vector<capture::CaptureGroup> groups; // by position
		};

		// The rig on the two inputs, each camera named by its footage's source and grouped by
		// position; or why there is none (InvalidDataset: footage and models that do not pair, or
		// footage that does not group), which the method's report says like any other refusal.
		std::variant<Rig, registration::FailureReason> rig(const flow::NodeEvaluation& evaluation) const;

	private:
		flow::PortId m_footage;				 // "footage" (std::vector<media::FrameSequence>)
		flow::PortId m_models;				 // "models" (std::vector<CameraModel>)
		flow::PortId m_reference;			 // "reference" (std::string, empty = automatic)
		flow::PortId m_unknownApplicability; // "unknownApplicability" (registration::ApplicabilityPolicy)
		flow::PortId m_loss;				 // "loss" (registration::LossFamily)
		flow::PortId m_lossScale;			 // "lossScale" (float, standard deviations)
		flow::PortId m_fitnessProfile;		 // "fitnessProfile" (std::string, empty = the method's own)
		flow::PortId m_heldOutFraction;		 // "heldOutFraction" (float)
		flow::PortId m_resamples;			 // "resamples" (int)
		flow::PortId m_seed;				 // "seed" (int)
		flow::PortId m_maximumIterations;	 // "maximumIterations" (int)
		flow::PortId m_deterministic;		 // "deterministic" (bool)
	};
} // namespace lain::camera
