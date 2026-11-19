#pragma once

#include <lain/camera/calibration/report.h>
#include <lain/flow/dynamicports.h>
#include <lain/flow/evaluation.h>

#include <string>
#include <vector>

namespace lain::camera
{
	// What the fallback nodes that read the attempts made share (nextAttempt, calibrationAttempts):
	// their attempts arrive as calibration reports on DYNAMIC input pins, added with the canvas "+" as
	// a Merge's branches are, and the order the pins were added is the priority order. Neither node
	// knows which method a pin carries, so a third method changes the wiring, not the nodes.
	//
	// Every pin is OPTIONAL: an attempt that was skipped (its gate off, so its method's node never ran)
	// has an empty report, and must suppress neither node. Empty at construction (DynamicPortsNode's
	// contract); a node's own static inputs are not attempts.
	class AttemptReportsNode : public flow::DynamicPortsNode
	{
	public:
		// Attempts grow the input side.
		flow::Port::Direction dynamicSide() const override { return flow::Port::Direction::Input; }

		// Only a calibration report is an attempt.
		bool acceptsPortType(const std::string& key) const override;

	protected:
		explicit AttemptReportsNode(std::string name);

		// The attempts made: the reports on the dynamic pins that carry one, in the order the pins were
		// added. Each is checked, not assumed: acceptsPortType is the HOST's filter, and a pin added
		// otherwise answers to nobody, so a pin of another type is no attempt rather than a wrongly
		// typed read.
		std::vector<const calibration::Report*> attempts(const flow::NodeEvaluation& evaluation) const;

		// The pin-adding route declares a Required pin, so each is made Optional here, however it
		// arrived.
		void onDynamicPortAdded(flow::PortId id) override;
	};
} // namespace lain::camera
