#pragma once

#include "lain/camera/flow/rigregistrationnode.h"

#include <lain/camera/board/specification.h>
#include <lain/flow/evaluation.h>

namespace lain::camera
{
	// Fixed-camera board registration (registration::board::registerCameras) over a rig's footage
	// and models (RigRegistrationNode: paired by position, named by source, grouped by position) and
	// the `board` they all see. Always a `report`, successful or failed.
	//
	// ONE node for the whole method, as calibrateCamera is: which groups fit and which validate, how
	// the rig is put together and what the verdict is held to are decisions taken together.
	//
	// `pixelSigma` is the noise of a corner whose detection gives no covariance. Detection settings
	// mean what they mean on detectBoard.
	class RegisterCamerasNode : public RigRegistrationNode
	{
	public:
		RegisterCamerasNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<RegisterCamerasNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_board;		   // "board" (board::Specification), input 2
		flow::PortId m_pixelSigma;	   // "pixelSigma" (float)
		flow::PortId m_longestSide;	   // "longestSide" (int, 0 = native)
		flow::PortId m_refineNative;   // "refineNative" (bool)
		flow::PortId m_minimumCorners; // "minimumCorners" (int)
		flow::PortId m_report;		   // "report" (registration::Report)
	};
} // namespace lain::camera
