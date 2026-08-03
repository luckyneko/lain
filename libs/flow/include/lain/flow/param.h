#pragma once

#include "lain/flow/porttype.h" // PortType — the per-type reflective flyweight (shared with Port)
#include "lain/flow/portvalue.h"
#include "lain/flow/types.h" // PortId

#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

namespace lain::flow
{
	// A named, typed CONFIGURATION value on a node — distinct from a Port. A Port is
	// dataflow (edge-driven, populated by the scheduler each run); a Param is a fixed
	// setting the node reads (a LoadImageNode's path, a BlurNode's radius), edited by the
	// adapter and left untouched by the scheduler. Created only by its owning Node
	// (addParam), and seeded with a default at declaration.
	//
	// A Param reuses the same internals as a Port — a PortValue slot + a PortType flyweight
	// — on purpose: the two share one value-erasure mechanism, so promoting a param to a
	// connectable input port later is a definition change, not a data change (that is how a
	// config becomes graph-driven — wire a constant node to the promoted input). flow stays
	// UI-free: a Param is pure data; the adapter (flowview) chooses an editor by type().
	class Param
	{
	public:
		const std::string& name() const { return m_name; }

		// This param's stable identity within its node (assigned by the owning Node at addParam),
		// drawn from the same per-node counter as its ports — so a node stores what its declaration
		// returned and reads it back through param(PortId), rather than holding a position that a
		// future dynamic param could shift.
		PortId id() const { return m_id; }

		// The declared type — what the adapter's editor registry dispatches on.
		std::type_index type() const { return m_type->index; }

		// Human-readable declared-type name (meta::typeName<T>) — for labels/debug.
		std::string_view typeName() const { return m_type->name; }

		// The current value as text (via the PortType flyweight's meta::toString bridge).
		std::string describe() const { return m_type->describe(m_value); }

		// Read the value. get<T>() throws std::bad_any_cast on a type mismatch.
		//
		// There is no write side here, deliberately. A param is recipe, so changing one must
		// invalidate its node — which only the node can do. Writes go through
		// Node::setParam(id, value), which type-checks, commits and invalidates as one operation.
		template <typename T>
		const T& get() const;
		template <typename T>
		bool holds() const
		{
			return m_value.holds<T>();
		}

		const PortValue& value() const { return m_value; }

	private:
		friend class Node; // only a Node builds its params — and only a Node writes one

		// Seed the declared default (addParam). Not part of the public surface: see the note above.
		template <typename T>
		void set(T value);
		Param(std::string name, const PortType& type, PortId id)
			: m_name(std::move(name))
			, m_type(&type)
			, m_id(id)
		{
		}

		std::string m_name;
		const PortType* m_type; // the declared type's shared reflective flyweight
		PortId m_id;			// stable within its node, from the same counter as the ports'
		PortValue m_value;		// seeded with the default at addParam<T>
	};
} // namespace lain::flow

#include "lain/flow/details/param.inl"
