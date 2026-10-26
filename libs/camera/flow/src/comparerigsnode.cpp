#include "lain/camera/flow/comparerigsnode.h"

#include <utility>

namespace lain::camera
{
	CompareRigsNode::CompareRigsNode()
		: Node("CompareRigs")
	{
		m_expected = addInput<registration::RegisteredRig>("expected");
		m_found = addInput<registration::RegisteredRig>("found");
		m_comparison = addOutput<RigComparison>("comparison");
	}

	void CompareRigsNode::compute(flow::NodeEvaluation& evaluation) const
	{
		RigComparison comparison = compareRigs(evaluation.input(m_expected).get<registration::RegisteredRig>(),
											   evaluation.input(m_found).get<registration::RegisteredRig>());
		if (!comparison.compared())
		{
			evaluation.fail(comparison.detail);
			return;
		}
		evaluation.output(m_comparison).set(std::move(comparison));
	}
} // namespace lain::camera
