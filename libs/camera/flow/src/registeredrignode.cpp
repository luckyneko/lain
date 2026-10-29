#include "lain/camera/flow/registeredrignode.h"

#include <optional>
#include <utility>

namespace lain::camera
{
	RegisteredRigNode::RegisteredRigNode()
		: Node("RegisteredRig")
	{
		m_report = addInput<registration::Report>("report");
		m_rig = addOutput<registration::RegisteredRig>("rig");
	}

	void RegisteredRigNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const registration::Report& report = evaluation.input(m_report).get<registration::Report>();
		if (std::optional<registration::RegisteredRig> rig = report.rig())
			evaluation.output(m_rig).set(std::move(*rig));
		else
			evaluation.output(m_rig).clear();
	}
} // namespace lain::camera
