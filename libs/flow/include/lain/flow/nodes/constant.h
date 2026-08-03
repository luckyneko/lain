#pragma once

#include "lain/flow/evaluation.h"
#include "lain/flow/node.h"

#include <utility>

namespace lain::flow
{
	// A constant source: emits a fixed value on `out`. Payload-generic (any copyable T), so it feeds
	// anything — a Gate's bool `enable`, a Select's int `selector`, or a data input. Its value is a
	// PARAM (not a bare member) so it serialises with the graph and the inspector renders an editor
	// for it by type (ADR-0005). No inputs, so it is always ready; after its first compute an
	// evaluation has it at the current version and skips it until setValue bumps that.
	template <typename T>
	class ConstantNode : public Node
	{
	public:
		explicit ConstantNode(T value = T{})
			: Node("Constant")
		{
			m_value = addParam<T>("value", std::move(value));
			m_out = addOutput<T>("out");
		}

		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(m_out).template set<T>(param(m_value).template get<T>());
		}

		const T& value() const { return param(m_value).template get<T>(); }
		// Commits through the node's own param seam, which invalidates as part of the write — so
		// this re-emits (and recomputes downstream) on the next run with no separate markDirty to
		// forget.
		void setValue(T value) { setParam(m_value, std::move(value)); }

	private:
		PortId m_value; // the "value" param
		PortId m_out;
	};
} // namespace lain::flow
