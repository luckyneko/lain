#pragma once

#include "lain/flow/node.h"
#include "lain/flow/port.h" // Presence

namespace lain::flow
{
	// Merge<T>: the downstream rejoin of conditional eval — forwards the first branch that has a value
	// (a, then b), or produces nothing if both are empty. Its branch inputs are OPTIONAL, so an empty
	// (gated-off) branch does not suppress it (ADR-0007); it picks whichever survived. Pair it with
	// two Gates to express an if/else: gate(cond, x) and gate(!cond, y) into a Merge.
	template <typename T>
	class MergeNode : public Node
	{
	public:
		MergeNode()
			: Node("Merge")
		{
			m_a = addInput<T>("a", Presence::Optional);
			m_b = addInput<T>("b", Presence::Optional);
			m_out = addOutput<T>("out");
		}

		void compute() override
		{
			if (input(m_a).ready())
				output(m_out).template set<T>(input(m_a).template get<T>());
			else if (input(m_b).ready())
				output(m_out).template set<T>(input(m_b).template get<T>());
			else
				output(m_out).clear();
		}

	private:
		PortIndex m_a = 0;
		PortIndex m_b = 0;
		PortIndex m_out = 0;
	};
} // namespace lain::flow
