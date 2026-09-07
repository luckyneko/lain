#include "lain/flow/example/comparenode.h"

namespace lain::flow::example
{
	CompareNode::CompareNode(Comparison op, float threshold)
		: Node("Compare")
	{
		m_a = addInput<float>("a");
		// A defaulted input, not a param: the threshold is exactly the kind of setting that
		// plausibly varies per element of a map, and a param cannot be driven by the graph (the
		// 2026-08-15 param audit's rule).
		m_b = addInput<float>("b", Default{threshold});
		m_op = addParam<Comparison>("op", op);
		m_result = addOutput<bool>("result");
	}

	void CompareNode::compute(NodeEvaluation& evaluation) const
	{
		if (evaluation.input(m_a).empty() || evaluation.input(m_b).empty())
			return; // nothing to compare; leave the output as it was

		const float a = evaluation.input(m_a).get<float>();
		const float b = evaluation.input(m_b).get<float>();

		bool result = false;
		switch (param(m_op).get<Comparison>())
		{
			case Comparison::Greater:
				result = a > b;
				break;
			case Comparison::GreaterEqual:
				result = a >= b;
				break;
			case Comparison::Less:
				result = a < b;
				break;
			case Comparison::LessEqual:
				result = a <= b;
				break;
		}
		evaluation.output(m_result).set(result);
	}
} // namespace lain::flow::example
