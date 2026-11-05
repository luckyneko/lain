#pragma once

#include "lain/camera/flow/attemptreportsnode.h"

namespace lain::camera
{
	// Whether the next calibration method should run (ADR-0016): `run` is true when the method is
	// `included` and no earlier attempt was accepted (calibration::accepts, the rule the attempts'
	// record asks too). It switches the gate in front of that method's node, so a method that is not
	// needed is never run.
	//
	// The earlier attempts' reports arrive on pins added with the canvas "+" (AttemptReportsNode), in
	// priority order. `included` defaults to true, so an unwired one reads naturally: run unless
	// something earlier was accepted. Wired to a calibrationMethod output, a suppressed one suppresses
	// `run`, and with it the method.
	class NextAttemptNode : public AttemptReportsNode
	{
	public:
		NextAttemptNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<NextAttemptNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_included; // "included" (bool), static: not an attempt
		flow::PortId m_run;		 // "run" (bool)
	};
} // namespace lain::camera
