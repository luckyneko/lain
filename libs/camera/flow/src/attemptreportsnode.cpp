#include "lain/camera/flow/attemptreportsnode.h"

#include <lain/flow/porttyperegistry.h>

namespace lain::camera
{
	AttemptReportsNode::AttemptReportsNode(std::string name)
		: DynamicPortsNode(std::move(name))
	{
	}

	bool AttemptReportsNode::acceptsPortType(const std::string& key) const
	{
		return flow::portTypeFor(key) == &flow::portType<calibration::Report>();
	}

	std::vector<const calibration::Report*> AttemptReportsNode::attempts(const flow::NodeEvaluation& evaluation) const
	{
		std::vector<const calibration::Report*> made;
		for (std::size_t i = 0; i < inputCount(); ++i)
		{
			const flow::Port& pin = input(i);
			if (!pin.isDynamic())
				continue; // a static input of the node's own, not an attempt
			const flow::PortValue& report = evaluation.input(pin.id());
			if (report.holds<calibration::Report>())
				made.push_back(&report.get<calibration::Report>());
		}
		return made;
	}

	void AttemptReportsNode::onDynamicPortAdded(flow::PortId id)
	{
		if (flow::Port* pin = findInput(id))
			pin->setRequired(false);
	}
} // namespace lain::camera
