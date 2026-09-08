#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/flow/porttype.h>

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

	// Two values in, a bool out — the other half of a WHILE loop's condition (M11 / ADR-0021):
	// ImageDifference measures, this decides, and the bool goes to the reserved `continue` pin.
	//
	// What it compares is a PAYLOAD TYPE named "value" (M12 / ADR-0022), not a fixed float. That is
	// what unblocked driving a loop's condition from its own `index`, which is an int — before it,
	// the only comparison in the tree was over floats and the two could not meet without a Cast.
	//
	// Its accepted types are DERIVED, never listed: acceptsPayloadType asks
	// PortType::isOrderable(), so every registered type with an `operator<` can be compared (Int,
	// Float, String, Path — lexicographic order on the last two is a real order) and an Image, which
	// has none, simply never appears in the menu. A list would have to be revisited every time the
	// app registered a type; this cannot fall behind.
	//
	// `b` carries a DEFAULT, so a threshold can be typed on the node (the common case: "keep going
	// while the change is above 0.002") or driven by the graph when something is wired to it —
	// flow::Default's own rule, and the reason a threshold need not be a Constant node. The default
	// is the payload type's own default value, so it means "zero" for a number and "" for a string.
	//
	// An empty input suppresses, as for any node. Inside a loop that means the iteration failed and
	// the fold BREAKS rather than terminating quietly (ADR-0021) — which is why the operator is a
	// param and not a second value input: a comparison that cannot be made must not be able to
	// answer `false` and look like ordinary convergence.
	class CompareNode : public Node
	{
	public:
		explicit CompareNode(const PortType& type = portType<float>(), Comparison op = Comparison::Greater);

		// The name of this node's payload type — what a host's dropdown and the serializer address.
		static constexpr const char* kValuePayload = "value";

		// Only a type that can be ORDERED. Asked of the capability rather than of a list, so the
		// answer grows with the registered types instead of rotting.
		bool acceptsPayloadType(const std::string& name, const PortType& type) const override;

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_a;		 // the measurement
		PortId m_b;		 // the threshold, with a Default
		PortId m_op;	 // param: which comparison
		PortId m_result; // bool
	};
} // namespace lain::flow::example
