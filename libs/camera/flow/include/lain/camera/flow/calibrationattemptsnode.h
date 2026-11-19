#pragma once

#include "lain/camera/flow/attemptreportsnode.h"

namespace lain::camera
{
	// The attempts a calibration fallback made, combined (ADR-0016): `attempts`, the reports on its pins
	// in priority order (AttemptReportsNode), and `accepted`, the accepted one's report
	// (calibration::Attempts::accepted, the rule nextAttempt asks too), for cameraModel to take the
	// model from.
	//
	// With no accepted attempt, `accepted` is cleared, so what uses the model is suppressed while
	// `attempts` still says why. With no attempt made at all (the footage missing upstream, or every
	// method left out) BOTH are cleared: no attempt is not an outcome, and the absence propagates
	// rather than reading as a failed fallback (ADR-0007).
	class CalibrationAttemptsNode : public AttemptReportsNode
	{
	public:
		CalibrationAttemptsNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<CalibrationAttemptsNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_attempts; // "attempts" (calibration::Attempts)
		flow::PortId m_accepted; // "accepted" (calibration::Report)
	};
} // namespace lain::camera
