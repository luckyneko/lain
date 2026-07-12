#pragma once

#include "lain/flow/node.h"
#include "lain/flow/port.h" // Presence

namespace lain::flow
{
	// Select<T>: routes by an int `selector` — 0 picks `a`, anything else `b`. The selector is
	// required; the branch inputs are OPTIONAL, so the unselected branch may be gated off (empty)
	// without suppressing the select (ADR-0007). Unlike Merge (first-live), Select is deterministic
	// by index — a mux. Produces nothing if the chosen branch is empty.
	template <typename T>
	class SelectNode : public Node
	{
	public:
		SelectNode()
			: Node("Select")
		{
			m_selector = addInput<int>("selector");
			m_a = addInput<T>("a", Presence::Optional);
			m_b = addInput<T>("b", Presence::Optional);
			m_out = addOutput<T>("out");
		}

		void compute() override
		{
			const Port& picked = (input(m_selector).template get<int>() == 0) ? input(m_a) : input(m_b);
			if (picked.ready())
				output(m_out).template set<T>(picked.template get<T>());
			else
				output(m_out).clear();
		}

	private:
		PortIndex m_selector = 0;
		PortIndex m_a = 0;
		PortIndex m_b = 0;
		PortIndex m_out = 0;
	};
} // namespace lain::flow
