#pragma once

#include "lain/flow/evaluation.h"
#include "lain/flow/node.h"

namespace lain::flow
{
	// Gate<T>: passes `value` to `out` when `enable` is true, else produces NO value (out is cleared).
	// The upstream suppressor of conditional eval (ADR-0007): placed *before* an expensive subtree, an
	// off gate leaves that subtree's input empty, so the readiness gate suppresses it — no wasted work.
	//
	// `enable` DEFAULTS TO TRUE, so a gate that has just been dropped on the canvas passes its value
	// through. Before defaults existed it was a plain required input, which made an unwired gate a
	// dead end: it was not ready, so it suppressed everything downstream and looked broken until you
	// found something to feed it. Wire it (a Constant<bool>, or any computed condition) to control it;
	// flip the default in the inspector to keep a gate off with nothing attached.
	//
	// `value` has no default, deliberately: it is the data flowing through, and inventing one would
	// mean a gate could produce a value nothing gave it. An off gate still needs `value` present to be
	// ready — but `value` is the cheap data, not the expensive result, which lives downstream.
	template <typename T>
	class GateNode : public Node
	{
	public:
		GateNode()
			: Node("Gate")
		{
			m_enable = addInput<bool>("enable", Default{true});
			m_value = addInput<T>("value");
			m_out = addOutput<T>("out");
		}

		void compute(NodeEvaluation& evaluation) const override
		{
			if (evaluation.input(m_enable).template get<bool>())
				evaluation.output(m_out).template set<T>(evaluation.input(m_value).template get<T>());
			else
				evaluation.output(m_out).clear(); // suppress — downstream sees no value
		}

	private:
		PortId m_enable;
		PortId m_value;
		PortId m_out;
	};
} // namespace lain::flow
