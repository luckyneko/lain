#include "lain/flow/example/comparenode.h"

namespace lain::flow::example
{
	CompareNode::CompareNode(const PortType& type, Comparison op)
		: Node("Compare")
	{
		addPayloadType(kValuePayload, type);
		m_a = addInputOf(kValuePayload, "a");
		// A defaulted input, not a param: the threshold is exactly the kind of setting that
		// plausibly varies per element of a map, and a param cannot be driven by the graph (the
		// 2026-08-15 param audit's rule). Its default is the payload type's own default value, so
		// the declaration needs no compile-time T to seed it.
		m_b = addDefaultedInputOf(kValuePayload, "b");
		m_op = addParam<Comparison>("op", op);
		m_result = addOutput<bool>("result"); // NOT payload-typed: a comparison answers a bool
	}

	bool CompareNode::acceptsPayloadType(const std::string&, const PortType& type) const
	{
		// The capability, not a list — so this grows with the registered types rather than needing
		// to be revisited each time the app adds one (ADR-0022). An Image has no operator<, so it
		// never appears in the menu and cannot be installed by a document either.
		return type.isOrderable();
	}

	void CompareNode::compute(NodeEvaluation& evaluation) const
	{
		if (evaluation.input(m_a).empty() || evaluation.input(m_b).empty())
			return; // nothing to compare; leave the output as it was

		// Three-way, through the payload type's own bridge — so this node names no payload type and
		// compares whatever it was configured with.
		const int order = payloadType(kValuePayload)->compare(evaluation.input(m_a), evaluation.input(m_b));

		bool result = false;
		switch (param(m_op).get<Comparison>())
		{
			case Comparison::Greater:
				result = order > 0;
				break;
			case Comparison::GreaterEqual:
				result = order >= 0;
				break;
			case Comparison::Less:
				result = order < 0;
				break;
			case Comparison::LessEqual:
				result = order <= 0;
				break;
		}
		evaluation.output(m_result).set(result);
	}
} // namespace lain::flow::example
