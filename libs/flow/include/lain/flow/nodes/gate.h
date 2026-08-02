#pragma once

#include "lain/flow/node.h"

namespace lain::flow
{
	// Gate<T>: passes `value` to `out` when `enable` is true, else produces NO value (out is cleared).
	// The upstream suppressor of conditional eval (ADR-0007): placed *before* an expensive subtree, an
	// off gate leaves that subtree's input empty, so the readiness gate suppresses it — no wasted work.
	// Both inputs are required (feed `enable` a Constant<bool> or a computed condition); an off gate
	// still needs `value` present to be ready, but `value` is the cheap data flowing through, not the
	// expensive result, which lives downstream.
	template <typename T>
	class GateNode : public Node
	{
	public:
		GateNode()
			: Node("Gate")
		{
			m_enable = addInput<bool>("enable");
			m_value = addInput<T>("value");
			m_out = addOutput<T>("out");
		}

		void compute() override
		{
			if (input(m_enable).template get<bool>())
				output(m_out).template set<T>(input(m_value).template get<T>());
			else
				output(m_out).clear(); // suppress — downstream sees no value
		}

	private:
		PortId m_enable;
		PortId m_value;
		PortId m_out;
	};
} // namespace lain::flow
