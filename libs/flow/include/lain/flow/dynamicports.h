#pragma once

// A node that opts into DYNAMIC PORTS — pins added / removed at runtime (M4 vertical b). The gui
// dynamic_casts to this to show its ± affordance; a fixed node simply doesn't derive it and is
// unaffected. Every pin added here is on one side (dynamicSide) and carries a stable PortId, so an
// edge or a boundary handle survives sibling pins being added / removed / reordered.
//
// This base only ADDS pins (adding touches no edges). Removal is a Graph *primitive*
// (Graph::removePort, which refuses a still-connected pin) orchestrated by the *safe gesture*
// edit::removePort (disconnect incident edges, then the primitive) — the primitive/policy split
// the rest of flow uses.
//
// Serialization contract: a DynamicPortsNode's FACTORY-constructed form (what the node factory
// builds) has an EMPTY dynamic side — every dynamic-side pin is added at runtime and serialized /
// replayed on load. A subclass ctor must not pre-add pins on dynamicSide() (a GroupInputNode's
// construction-time boundaries go through the same runtime addDynamicPort path, so they serialize
// like any other), else load would double-add.

#include "lain/flow/node.h"
#include "lain/flow/port.h"
#include "lain/flow/types.h"

#include <string>
#include <utility>

namespace lain::flow
{
	class DynamicPortsNode : public Node
	{
	public:
		// The side pins are added to — all of a node's dynamic pins share one side (a GroupInput
		// grows its outputs, a Merge its inputs).
		virtual Port::Direction dynamicSide() const = 0;

		// Whether this node accepts a runtime pin of the registered port type `key`. A dynamic
		// node's "+" menu is the port-type registry's keys filtered by this — default accepts any
		// registered type; a homogeneous node (a Merge) narrows it to one.
		virtual bool acceptsPortType(const std::string& /*key*/) const { return true; }

		// Add a pin of type T on the dynamic side; returns its stable PortId. The port-type
		// registry's creators call this (T captured in the closure), so the gui stays type-agnostic.
		template <typename T>
		PortId addDynamicPort(std::string name)
		{
			// Reject an invalid or duplicate name on the dynamic side (returns a null PortId): a
			// runtime pin's name may be user-supplied, so it rejects gracefully rather than asserting
			// like a static port. Name-addressed serialization + cli flags need pins validly + uniquely
			// named.
			if (!validPortName(name) || hasPortNamed(dynamicSide(), name))
				return PortId{};

			const bool onOutput = dynamicSide() == Port::Direction::Output;
			const PortId id = onOutput ? addOutput<T>(std::move(name)) : addInput<T>(std::move(name));
			// Mark it dynamic so serialization replays it (and not a static ctor-declared sibling
			// pin). Reached by the id the declaration just returned, not by a position.
			(onOutput ? output(id) : input(id)).markDynamic();
			onDynamicPortAdded(id);
			return id;
		}

	protected:
		explicit DynamicPortsNode(std::string name)
			: Node(std::move(name))
		{
		}

		// Hook for per-node bookkeeping when a dynamic pin is added (e.g. GroupInputNode's
		// bound-value slot). Default no-op. Removal needs no matching hook: a stale per-pin entry
		// keyed by the retired PortId is harmless — ids are never reused — and a node's compute()
		// guards on the live port (findOutput / findInput).
		virtual void onDynamicPortAdded(PortId /*id*/) {}
	};
} // namespace lain::flow
