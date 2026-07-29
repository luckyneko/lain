#pragma once

#include "lain/flow/porttype.h" // PortType — the per-type reflective flyweight
#include "lain/flow/portvalue.h"
#include "lain/flow/types.h" // PortId

#include <cctype>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

namespace lain::flow
{
	// A port name must be an identifier: a LETTER, then any letters / digits / underscores. Names
	// double as stable identifiers (edges + cli flags address boundary pins by name — `--source`),
	// so a leading digit or underscore (`--2x`, `--_x`) is disallowed. Enforced at the add seams:
	// addInput/addOutput assert it (an author bug), addDynamicPort + a boundary rename reject it.
	inline bool validPortName(std::string_view name)
	{
		if (name.empty() || std::isalpha(static_cast<unsigned char>(name.front())) == 0)
			return false;
		for (const char c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_')
				return false;
		}
		return true;
	}

	// A named, typed slot on a node. A Port carries its declared (static) type for
	// connection checking and owns a persistent PortValue — the runtime payload,
	// overwritten in place on recompute and left intact for the inspector to read.
	// Created only by its owning Node (addInput / addOutput).
	class Port
	{
	public:
		enum class Direction
		{
			Input,
			Output,
		};

		const std::string& name() const { return m_name; }
		Direction direction() const { return m_dir; }

		// Rename the port — display only. Edges and boundary handles reference the PortId, so a
		// rename touches nothing structural (used for editable boundary-pin names).
		void setName(std::string name) { m_name = std::move(name); }

		// The port's stable identity within its node (assigned by the owning Node). Edges and
		// boundary handles reference this, so it survives the port being reordered/renumbered —
		// unlike its index.
		PortId id() const { return m_id; }

		// The declared type, fixed at declaration — what connect() type-checks
		// against (independent of whether a value has been produced yet).
		std::type_index type() const { return m_type->index; }

		// A human-readable name for the declared type (meta::typeName<T>, a string_view
		// into static storage). For display/debug — the inspector labels pins with it.
		std::string_view typeName() const { return m_type->name; }

		// The declared type's reflective flyweight itself — for MIRRORING this port's type onto
		// another node without knowing T (Node::addInputLike / addOutputLike, which is how a group
		// derives its ports from its inner boundary pins). One shared instance per type, so passing
		// it around is just a pointer copy.
		const PortType& portType() const { return *m_type; }

		// True once a value has been produced into this port (i.e. not empty).
		bool ready() const { return !m_value.empty(); }

		// This port's current value as a human-readable string: "(empty)" when unset, the
		// value via meta::toString (its toString() / ostream, else its type name). The
		// renderer comes from the port's PortType flyweight, captured from the declared
		// type T at addInput/addOutput<T> — so a type-erased value is described with no
		// central type ladder; a new type displays itself just by exposing toString().
		// For text/debug (cli dump, inspector labels); a richer GUI view of a value (e.g.
		// a texture thumbnail) is a separate, per-medium concern.
		std::string describe() const { return m_type->describe(m_value); }

		template <typename T>
		void set(T value);
		template <typename T>
		const T& get() const;
		template <typename T>
		bool holds() const { return m_value.holds<T>(); }

		const PortValue& value() const { return m_value; }
		PortValue& value() { return m_value; }
		void clear() { m_value.clear(); }

		// Conditional / gated eval (WORK.md Tier A #2, ADR-0007). A node is READY (computes) iff all
		// its REQUIRED inputs carry a value; otherwise the scheduler clears its outputs, and that
		// emptiness suppresses downstream. `required()` (input ports) is that gate: a Required input
		// (the default) must have a value; an Optional one need not — a Select/Merge branch, which its
		// compute() checks for presence and picks a live one. A Gate suppresses simply by clear()ing
		// its output. So "skip" is just absence-of-value; there is no separate skipped state.
		bool required() const { return m_required; }

		// Change this input's presence after construction. A dynamic node whose branches must be
		// Optional (a variadic Merge / Select) flips each pin here in onDynamicPortAdded, since the
		// port-type registry's creator adds Required pins — so a branch is Optional however it was
		// added (the "+" menu, addDynamicPort, or serialize-replay). Public for the same reason
		// setName is: friendship isn't inherited, so a Node *subclass* needs the mutator.
		void setRequired(bool required) { m_required = required; }

		// Whether this pin was added at RUNTIME (DynamicPortsNode::addDynamicPort) rather than declared
		// in the node's constructor. Serialization replays only the dynamic pins — a STATIC pin a node
		// declares in its ctor on the dynamic side (a Select's `selector` input among its dynamic
		// branches) is rebuilt by the ctor on load, so it must not be replayed (else it double-adds).
		bool isDynamic() const { return m_dynamic; }
		void markDynamic() { m_dynamic = true; } // set by addDynamicPort (public — friendship isn't inherited)

	private:
		friend class Node; // only a Node builds its ports (and assigns the id)
		Port(std::string name, Direction dir, const PortType& type, PortId id, bool required = true)
			: m_name(std::move(name))
			, m_dir(dir)
			, m_type(&type)
			, m_id(id)
			, m_required(required)
		{
		}

		std::string m_name;
		Direction m_dir;
		const PortType* m_type; // the declared type's shared reflective flyweight
		PortId m_id;			// stable identity within the owning node
		PortValue m_value;		//
		bool m_required = true; // input only: an empty Required input keeps the node from being ready
		bool m_dynamic = false; // added at runtime via addDynamicPort (vs declared in the node's ctor)
	};

	// Whether an input must carry a value for its node to be ready (see Port::required). On outputs
	// it is meaningless (a node's readiness is gated by what it consumes) — addOutput takes no such flag.
	enum class Presence
	{
		Required, // default: an empty value on this input suppresses the node
		Optional, // the node can run without it (a Select/Merge branch) — compute() checks presence
	};
} // namespace lain::flow

#include "lain/flow/details/port.inl"
