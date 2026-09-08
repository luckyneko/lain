#pragma once

#include "lain/flow/evaluation.h"
#include "lain/flow/node.h"
#include "lain/flow/porttype.h"

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
	//
	// What it passes is a PAYLOAD TYPE (ADR-0022), named "value". This used to be a template, and the
	// app registered it for image::Image alone — so a graph could not gate a float, a flag or a path.
	// `enable` is NOT declared from the payload type and a retype leaves it exactly where it is: a
	// gate is switched by a bool whatever it carries.
	class GateNode : public Node
	{
	public:
		explicit GateNode(const PortType& type)
			: Node("Gate")
		{
			addPayloadType(kValuePayload, type);
			m_enable = addInput<bool>("enable", Default{true});
			m_value = addInputOf(kValuePayload, "value");
			m_out = addOutputOf(kValuePayload, "out");
		}

		// The name of this node's payload type — what a host's dropdown and the serializer address.
		static constexpr const char* kValuePayload = "value";

		void compute(NodeEvaluation& evaluation) const override
		{
			// A whole-slot copy. This node never knew what it was passing; now it does not have to
			// name a type to say so, and copying a PortValue is a refcount bump.
			if (evaluation.input(m_enable).get<bool>())
				evaluation.output(m_out) = evaluation.input(m_value);
			else
				evaluation.output(m_out).clear(); // suppress — downstream sees no value
		}

	private:
		PortId m_enable;
		PortId m_value;
		PortId m_out;
	};
} // namespace lain::flow
