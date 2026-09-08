#pragma once

#include "lain/flow/dynamicports.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/porttype.h"
#include "lain/flow/porttyperegistry.h" // portTypeFor — homogeneous "+" filter

#include <typeindex>
#include <typeinfo>

namespace lain::flow
{
	// Variadic Select: an int `selector` input picks one of N dynamic branch inputs (0-based) and
	// forwards it to `out`. Deterministic by index — a mux, cf. Merge's first-live. Its branches are
	// OPTIONAL, so the unpicked ones may be gated off (empty) without suppressing the select (ADR-0007).
	//
	// The selector is a proper INPUT (not a param), so it can be driven by the graph — wire a
	// Constant of type Int for a fixed choice, or any int-producing node for data-driven routing. It is
	// Optional too: unconnected, it defaults to 0 (pick the first branch). It is a STATIC pin (declared
	// here in the ctor), so it sits among the dynamic branches on the input side but is NOT a branch —
	// compute() routes over the dynamic pins only, and serialization rebuilds it from the ctor rather
	// than replaying it (Port::isDynamic). Homogeneous branches (only its payload type is addable);
	// empty of branches at construction. Produces nothing when the selector is out of range or the
	// picked branch is empty.
	//
	// What it routes is a PAYLOAD TYPE (ADR-0022), named "value". The `selector` is NOT declared from
	// it and a retype leaves it alone: a select is indexed by an int whatever it carries.
	class SelectNode : public DynamicPortsNode
	{
	public:
		explicit SelectNode(const PortType& type)
			: DynamicPortsNode("Select")
		{
			addPayloadType(kValuePayload, type);
			m_selector = addInput<int>("selector", Default{0}); // unwired -> branch 0
			m_out = addOutputOf(kValuePayload, "out");
		}

		// The name of this node's payload type — what a host's dropdown and the serializer address.
		static constexpr const char* kValuePayload = "value";

		// Branches grow the input side (the selector is a static input, not a branch).
		Port::Direction dynamicSide() const override { return Port::Direction::Input; }

		// Homogeneous — only this node's payload type may be added as a branch.
		bool acceptsPortType(const std::string& key) const override
		{
			return portTypeFor(key) == payloadType(kValuePayload);
		}

		void compute(NodeEvaluation& evaluation) const override
		{
			// Unwired, the slot carries the declared default, so there is no presence check here. And
			// if the selector IS wired to something that produced nothing, this node is not ready and
			// compute never runs — a suppressed selector suppresses, rather than quietly routing to
			// branch 0 as an Optional input used to.
			const int sel = evaluation.input(m_selector).get<int>();
			const PortType* type = payloadType(kValuePayload);
			// The branches are the dynamic input pins, in add order; route to the sel-th (the static
			// selector pin is skipped, so its position among the inputs doesn't shift the indexing).
			int branchIndex = 0;
			for (std::size_t i = 0; i < inputCount(); ++i)
			{
				const Port& in = input(i);
				if (!in.isDynamic())
					continue; // the selector, not a branch
				if (branchIndex == sel)
				{
					const PortValue& branch = evaluation.input(in.id());
					// Type-checked for the reason Merge's is: acceptsPortType is the host's filter,
					// so a mistyped branch must produce nothing rather than reach a slot that
					// declares another type.
					if (!branch.empty() && branch.type() == type->index)
						evaluation.output(m_out) = branch;
					else
						evaluation.output(m_out).clear();
					return;
				}
				++branchIndex;
			}
			evaluation.output(m_out).clear(); // selector out of range (or negative) — nothing to route
		}

	protected:
		// Branches are Optional so the unpicked ones don't block readiness. The registry creator adds
		// Required pins, so flip each here — however the pin arrived. And TAG it, so a retype of this
		// node's payload type moves the branch too.
		void onDynamicPortAdded(PortId id) override
		{
			if (Port* p = findInput(id))
				p->setRequired(false);
			tagAs(id, kValuePayload);
		}

	private:
		PortId m_selector; // the static "selector" input — not one of the dynamic branches
		PortId m_out;
	};
} // namespace lain::flow
