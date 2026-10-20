#pragma once

#include <lain/camera/calibration/attempts.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

namespace lain::camera
{
	// Which calibration methods a graph's `method` includes (calibration::includes): `board` and
	// `targetless`, each a bool that switches the gate in front of that method's node (ADR-0016). A
	// method a selection leaves out is never run, by suppression: its gate is off, so its footage is
	// empty and its node is not ready.
	//
	// `method` defaults to BoardThenTargetless, so a fallback dropped on the canvas tries the board
	// first. It is an input, so a graph's boundary can choose; a suppressed one suppresses both outputs,
	// rather than quietly choosing for it.
	class CalibrationMethodNode : public flow::Node
	{
	public:
		CalibrationMethodNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<CalibrationMethodNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_method;	   // "method" (calibration::Method)
		flow::PortId m_board;	   // "board" (bool)
		flow::PortId m_targetless; // "targetless" (bool)
	};
} // namespace lain::camera
