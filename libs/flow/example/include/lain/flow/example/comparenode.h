#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

namespace lain::flow::example
{
	// How CompareNode compares. Only the four INEQUALITIES: exact float equality would need an
	// epsilon policy, and inventing one with nothing asking for it is the guessing ADR-0012 warns
	// against — "is it close enough?" is spelled `Less` against a tolerance, which is the shape a
	// convergence test has anyway.
	enum class Comparison
	{
		Greater,
		GreaterEqual,
		Less,
		LessEqual,
	};

	// Two numbers in, a bool out — the other half of a WHILE loop's condition (M11 / ADR-0021):
	// ImageDifference measures, this decides, and the bool goes to the reserved `continue` pin.
	//
	// `b` carries a DEFAULT, so a threshold can be typed on the node (the common case: "keep going
	// while the change is above 0.002") or driven by the graph when something is wired to it —
	// flow::Default's own rule, and the reason a threshold need not be a Constant node.
	//
	// An empty input suppresses, as for any node. Inside a loop that means the iteration failed and
	// the fold BREAKS rather than terminating quietly (ADR-0021) — which is why the operator is a
	// param and not a second value input: a comparison that cannot be made must not be able to
	// answer `false` and look like ordinary convergence.
	class CompareNode : public Node
	{
	public:
		explicit CompareNode(Comparison op = Comparison::Greater, float threshold = 0.0f);

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_a;		 // float — the measurement
		PortId m_b;		 // float, with a Default — the threshold
		PortId m_op;	 // param: which comparison
		PortId m_result; // bool
	};
} // namespace lain::flow::example
