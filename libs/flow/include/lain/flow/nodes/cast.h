#pragma once

#include "lain/flow/evaluation.h"
#include "lain/flow/node.h"
#include "lain/flow/porttype.h"
#include "lain/flow/porttyperegistry.h" // convertValue — the registry this node exists to read

namespace lain::flow
{
	// A CAST: one value in, the same value as another type out (ADR-0022). Two payload types —
	// `from` (its input) and `to` (its output) — so the node a graph needs to get an `int` into a
	// `float` input is authored, not compiled.
	//
	// This is the node the whole payload-type mechanism exists for. A per-type Cast class would need
	// one factory key per PAIR, which is not viable at any number of types; Constant, Gate, Merge and
	// Select merely benefit from what this forced into existence.
	//
	// It is the only reader of the value-conversion registry besides a host's menus. `connect` never
	// consults it: an edge type-checks EXACTLY, and a conversion is something a graph asks for by
	// containing this node — never something an edge performs on its behalf (ADR-0020's "no silent
	// lossy conversion", arriving from the other side).
	//
	// An unconvertible PAIR is representable and produces nothing. That is deliberate: refusing one
	// through acceptsPayloadType would make loading ORDER-DEPENDENT, since the loader applies one
	// payload type at a time — a document saying Path -> String would have its `from` refused while
	// `to` was still the factory's preset, and come back as a different node than it was saved as.
	// So the pair is checked where it can be answered whole (canConvert), and a host reports it.
	class CastNode : public Node
	{
	public:
		CastNode(const PortType& from, const PortType& to)
			: Node("Cast")
		{
			addPayloadType(kFromPayload, from);
			addPayloadType(kToPayload, to);
			m_in = addInputOf(kFromPayload, "in");
			m_out = addOutputOf(kToPayload, "out");
		}

		// The names of this node's two payload types. Public because a host's dropdowns and the
		// serializer both address them by name, and a string spelled in three places differs in one.
		static constexpr const char* kFromPayload = "from";
		static constexpr const char* kToPayload = "to";

		void compute(NodeEvaluation& evaluation) const override
		{
			// An empty result — no conversion for this pair, or one that could not convert this
			// value (an unparseable string) — suppresses downstream like any other node that
			// produced nothing (ADR-0007). The input is Required, so an empty INPUT never reaches
			// here at all: the readiness gate already stopped it.
			evaluation.output(m_out) = convertValue(evaluation.input(m_in), *payloadType(kToPayload));
		}

		// Whether this node's payload types are a pair the registry can actually convert. False is a
		// configuration a user can fix (pick a different `to`), not a failure, which is why it is a
		// question a host asks rather than something compute() logs once per run.
		//
		// A type converts to ITSELF, so a Cast whose two payload types are equal is a legal
		// pass-through rather than a broken node.
		bool canConvert() const
		{
			const PortType* from = payloadType(kFromPayload);
			const PortType* to = payloadType(kToPayload);
			return from->index == to->index || conversionRegistered(from->index, to->index);
		}

	private:
		PortId m_in;
		PortId m_out;
	};
} // namespace lain::flow
