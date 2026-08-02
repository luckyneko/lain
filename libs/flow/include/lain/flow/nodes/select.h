#pragma once

#include "lain/flow/dynamicports.h"
#include "lain/flow/porttyperegistry.h" // portTypeKey — homogeneous "+" filter

#include <typeindex>
#include <typeinfo>

namespace lain::flow
{
	// Variadic Select<T>: an int `selector` input picks one of N dynamic branch inputs (0-based) and
	// forwards it to `out`. Deterministic by index — a mux, cf. Merge's first-live. Its branches are
	// OPTIONAL, so the unpicked ones may be gated off (empty) without suppressing the select (ADR-0007).
	//
	// The selector is a proper INPUT (not a param), so it can be driven by the graph — wire a
	// ConstantNode<int> for a fixed choice, or any int-producing node for data-driven routing. It is
	// Optional too: unconnected, it defaults to 0 (pick the first branch). It is a STATIC pin (declared
	// here in the ctor), so it sits among the dynamic branches on the input side but is NOT a branch —
	// compute() routes over the dynamic pins only, and serialization rebuilds it from the ctor rather
	// than replaying it (Port::isDynamic). Homogeneous branches (only T's registered port type is
	// addable); empty of branches at construction. Produces nothing when the selector is out of range or
	// the picked branch is empty.
	template <typename T>
	class SelectNode : public DynamicPortsNode
	{
	public:
		SelectNode()
			: DynamicPortsNode("Select")
		{
			m_selector = addInput<int>("selector", Presence::Optional); // unconnected -> default branch 0
			m_out = addOutput<T>("out");
		}

		// Branches grow the input side (the selector is a static input, not a branch).
		Port::Direction dynamicSide() const override { return Port::Direction::Input; }

		// Homogeneous — only this T's registered key may be added as a branch.
		bool acceptsPortType(const std::string& key) const override
		{
			return key == portTypeKey(std::type_index(typeid(T)));
		}

		void compute() override
		{
			const int sel = input(m_selector).ready() ? input(m_selector).template get<int>() : 0;
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
					if (in.ready())
						output(m_out).template set<T>(in.template get<T>());
					else
						output(m_out).clear();
					return;
				}
				++branchIndex;
			}
			output(m_out).clear(); // selector out of range (or negative) — nothing to route
		}

	protected:
		// Branches are Optional so the unpicked ones don't block readiness. The registry creator adds
		// Required pins, so flip each here — however the pin arrived.
		void onDynamicPortAdded(PortId id) override
		{
			if (Port* p = findInput(id))
				p->setRequired(false);
		}

	private:
		PortId m_selector; // the static "selector" input — not one of the dynamic branches
		PortId m_out;
	};
} // namespace lain::flow
