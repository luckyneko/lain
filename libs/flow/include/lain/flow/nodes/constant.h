#pragma once

#include "lain/flow/evaluation.h"
#include "lain/flow/node.h"
#include "lain/flow/porttype.h"

#include <memory>
#include <utility>

namespace lain::flow
{
	// A constant source: emits a fixed value on `out`. Payload-generic, so it feeds anything — a
	// Gate's bool `enable`, a Select's int `selector`, or a data input. Its value is a PARAM (not a
	// bare member) so it serialises with the graph and the inspector renders an editor for it by
	// type (ADR-0005). No inputs, so it is always ready; after its first compute an evaluation has
	// it at the current version and skips it until setValue bumps that.
	//
	// The type it emits is a PAYLOAD TYPE (ADR-0022), named "value" — chosen by whoever authored the
	// graph and changed through edit::setPayloadType, rather than baked into the class. It used to
	// be a template, which cost the palette one entry per type (constInt, constBool, constFloat,
	// constPath, constString) and grew with every type an app registered.
	//
	// Nothing here knows what T is, and it never did: compute() copies the param's slot onto the
	// output, and copying a PortValue is a refcount bump. That is why this node needed no per-type
	// behaviour at all to stop being a template — unlike a Cast (a conversion registry) or a Compare
	// (the PortType ordering capability).
	class ConstantNode : public Node
	{
	public:
		explicit ConstantNode(const PortType& type)
			: Node("Constant")
		{
			addPayloadType(kValuePayload, type);
			m_value = addParamOf(kValuePayload, "value");
			m_out = addOutputOf(kValuePayload, "out");
		}

		// The name of this node's payload type. Public because a host's dropdown and the serializer
		// both address it by name, and a string spelled in three places eventually differs in one.
		static constexpr const char* kValuePayload = "value";

		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(m_out) = param(m_value).value();
		}

		// The emitted value, type-erased — what a host reads without knowing the payload type.
		const PortValue& value() const { return param(m_value).value(); }

		// The emitted value as T; throws std::bad_any_cast if that is not the payload type.
		template <typename T>
		const T& value() const
		{
			return param(m_value).get<T>();
		}

		// Commits through the node's own param seam, which invalidates as part of the write — so
		// this re-emits (and recomputes downstream) on the next run with no separate markDirty to
		// forget. Returns false, changing nothing, if T is not this node's payload type: "the type
		// is the schema" applies here exactly as it does to any param.
		template <typename T>
		bool setValue(T value)
		{
			return setParam(m_value, std::move(value));
		}

		// The param carrying the value, for a host that edits it through the generic param path.
		PortId valueParam() const { return m_value; }

	private:
		PortId m_value; // the "value" param
		PortId m_out;
	};

	// A constant of a COMPILE-TIME type, seeded with `value` — the direct replacement for the old
	// ConstantNode<T>{value}, and what a test or a hand-built graph wants. It needs no port-type
	// registry: a payload type is a PortType flyweight, and portType<T>() is always available.
	template <typename T>
	std::unique_ptr<ConstantNode> constantOf(T value)
	{
		auto node = std::make_unique<ConstantNode>(portType<T>());
		node->setValue(std::move(value));
		return node;
	}
} // namespace lain::flow
