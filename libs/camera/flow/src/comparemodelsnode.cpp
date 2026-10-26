#include "lain/camera/flow/comparemodelsnode.h"

#include <utility>

namespace lain::camera
{
	CompareModelsNode::CompareModelsNode()
		: Node("CompareModels")
	{
		m_expected = addInput<CameraModel>("expected");
		m_found = addInput<CameraModel>("found");
		m_comparison = addOutput<ModelComparison>("comparison");
	}

	void CompareModelsNode::compute(flow::NodeEvaluation& evaluation) const
	{
		ModelComparison comparison =
			compareModels(evaluation.input(m_expected).get<CameraModel>(), evaluation.input(m_found).get<CameraModel>());
		if (!comparison.compared())
		{
			evaluation.fail(comparison.detail);
			return;
		}
		evaluation.output(m_comparison).set(std::move(comparison));
	}
} // namespace lain::camera
