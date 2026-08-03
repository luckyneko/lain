#pragma once

#include "lain/flow/dynamicports.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/porttyperegistry.h" // portTypeKey — homogeneous "+" filter

#include <typeindex>
#include <typeinfo>

namespace lain::flow
{
	// Variadic Merge<T>: N dynamic branch inputs of type T, forwards the FIRST live (ready) one to
	// `out`. The rejoin after an if/else — its branches are OPTIONAL, so a gated-off (empty) branch
	// doesn't suppress the merge (ADR-0007); it fires on whichever branch carries a value. Homogeneous:
	// only T's registered port type is addable. Empty at construction (the dynamic-side contract) —
	// grow it via the "+" menu / addDynamicPort<T>. Produces nothing when no branch is live.
	template <typename T>
	class MergeNode : public DynamicPortsNode
	{
	public:
		MergeNode()
			: DynamicPortsNode("Merge")
		{
			m_out = addOutput<T>("out");
		}

		// Branches grow the input side.
		Port::Direction dynamicSide() const override { return Port::Direction::Input; }

		// Homogeneous — only this T's registered key may be added.
		bool acceptsPortType(const std::string& key) const override
		{
			return key == portTypeKey(std::type_index(typeid(T)));
		}

		void compute(NodeEvaluation& evaluation) const override
		{
			for (std::size_t i = 0; i < inputCount(); ++i)
			{
				const PortValue& branch = evaluation.input(input(i).id());
				if (!branch.empty())
				{
					evaluation.output(m_out).template set<T>(branch.template get<T>());
					return; // first live branch wins
				}
			}
			evaluation.output(m_out).clear(); // no live branch — produce nothing (suppresses downstream)
		}

	protected:
		// Every branch is Optional, so a missing / gated one doesn't block readiness (the merge fires
		// on the first live branch, not all of them). The registry creator adds Required pins, so flip
		// each here — however the pin arrived.
		void onDynamicPortAdded(PortId id) override
		{
			if (Port* p = findInput(id))
				p->setRequired(false);
		}

	private:
		PortId m_out;
	};
} // namespace lain::flow
