#pragma once

#include <lain/camera/board/specification.h>
#include <lain/camera/cameramodel.h>
#include <lain/camera/registration/report.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/media/framesequence.h>

#include <vector>

namespace lain::camera
{
	// Fixed-camera board registration (registration::board::registerCameras) over a rig's footage:
	// `footage` and `models` are collections, paired by position, so one wire carries every camera
	// and the inputs compose with a map (ADR-0016, amended). Frame k of every camera is capture group
	// k (by-position grouping), which is right for a frame-locked rig and only for one. Always a
	// `report`, successful or failed.
	//
	// ONE node for the whole method, as calibrateCamera is: which groups fit and which validate, how
	// the rig is put together and what the verdict is held to are decisions taken together.
	//
	// Each camera's identity is its footage's canonical source uri, a stand-in until a capture
	// manifest assigns identities. `reference` names one by that uri; empty chooses automatically.
	// Detection settings mean what they mean on detectBoard.
	class RegisterCamerasNode : public flow::Node
	{
	public:
		RegisterCamerasNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<RegisterCamerasNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_footage;				 // "footage" (std::vector<media::FrameSequence>)
		flow::PortId m_models;				 // "models" (std::vector<CameraModel>)
		flow::PortId m_board;				 // "board" (board::Specification)
		flow::PortId m_reference;			 // "reference" (std::string, empty = automatic)
		flow::PortId m_unknownApplicability; // "unknownApplicability" (registration::ApplicabilityPolicy)
		flow::PortId m_pixelSigma;			 // "pixelSigma" (float)
		flow::PortId m_loss;				 // "loss" (registration::LossFamily)
		flow::PortId m_lossScale;			 // "lossScale" (float, standard deviations)
		flow::PortId m_fitnessProfile;		 // "fitnessProfile" (std::string)
		flow::PortId m_heldOutFraction;		 // "heldOutFraction" (float)
		flow::PortId m_resamples;			 // "resamples" (int)
		flow::PortId m_seed;				 // "seed" (int)
		flow::PortId m_maximumIterations;	 // "maximumIterations" (int)
		flow::PortId m_deterministic;		 // "deterministic" (bool)
		flow::PortId m_longestSide;			 // "longestSide" (int, 0 = native)
		flow::PortId m_refineNative;		 // "refineNative" (bool)
		flow::PortId m_minimumCorners;		 // "minimumCorners" (int)
		flow::PortId m_report;				 // "report" (registration::Report)
	};
} // namespace lain::camera
