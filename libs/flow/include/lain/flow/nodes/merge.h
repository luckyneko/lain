#pragma once

#include "lain/flow/dynamicports.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/porttype.h"
#include "lain/flow/porttyperegistry.h" // portTypeFor — homogeneous "+" filter

#include <typeindex>
#include <typeinfo>

namespace lain::flow
{
	// Variadic Merge: N dynamic branch inputs, forwards the FIRST live (ready) one to `out`. The
	// rejoin after an if/else — its branches are OPTIONAL, so a gated-off (empty) branch doesn't
	// suppress the merge (ADR-0007); it fires on whichever branch carries a value. Homogeneous: only
	// its payload type is addable. Empty at construction (the dynamic-side contract) — grow it via
	// the "+" menu. Produces nothing when no branch is live.
	//
	// What it merges is a PAYLOAD TYPE (ADR-0022), named "value". This is the node that keeps the
	// mechanism honest: its branches are DYNAMIC pins, added at runtime through the port-type
	// registry rather than declared here, so each is TAGGED as it arrives and a retype moves the
	// whole fan-in — not just the output that happens to be declared in the constructor.
	class MergeNode : public DynamicPortsNode
	{
	public:
		explicit MergeNode(const PortType& type)
			: DynamicPortsNode("Merge")
		{
			addPayloadType(kValuePayload, type);
			m_out = addOutputOf(kValuePayload, "out");
		}

		// The name of this node's payload type — what a host's dropdown and the serializer address.
		static constexpr const char* kValuePayload = "value";

		// Branches grow the input side.
		Port::Direction dynamicSide() const override { return Port::Direction::Input; }

		// Homogeneous — only this node's payload type may be added.
		bool acceptsPortType(const std::string& key) const override
		{
			return portTypeFor(key) == payloadType(kValuePayload);
		}

		void compute(NodeEvaluation& evaluation) const override
		{
			const PortType* type = payloadType(kValuePayload);
			for (std::size_t i = 0; i < inputCount(); ++i)
			{
				const PortValue& branch = evaluation.input(input(i).id());
				// The type is checked, not assumed: acceptsPortType is the HOST's filter, and
				// addDynamicPort answers to nobody. A branch of the wrong type is not forwarded,
				// so a mistyped pin costs its own branch rather than putting a wrongly-typed value
				// in a slot that declares another.
				if (!branch.empty() && branch.type() == type->index)
				{
					evaluation.output(m_out) = branch; // a refcount bump, not a payload copy
					return;							   // first live branch wins
				}
			}
			evaluation.output(m_out).clear(); // no live branch — produce nothing (suppresses downstream)
		}

	protected:
		// Every branch is Optional, so a missing / gated one doesn't block readiness (the merge fires
		// on the first live branch, not all of them). The registry creator adds Required pins, so flip
		// each here — however the pin arrived. And TAG it, so a retype of this node's payload type
		// moves the branch too: the creator declared it with a compile-time T and knows nothing about
		// payload types.
		void onDynamicPortAdded(PortId id) override
		{
			if (Port* p = findInput(id))
				p->setRequired(false);
			tagAs(id, kValuePayload);
		}

	private:
		PortId m_out;
	};
} // namespace lain::flow
