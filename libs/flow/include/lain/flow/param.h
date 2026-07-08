#pragma once

#include "lain/flow/porttype.h" // PortType — the per-type reflective flyweight (shared with Port)
#include "lain/flow/portvalue.h"

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

		// The declared type — what the adapter's editor registry dispatches on.
		std::type_index type() const { return m_type->index; }

		// Human-readable declared-type name (meta::typeName<T>) — for labels/debug.
		std::string_view typeName() const { return m_type->name; }

		// The current value as text (via the PortType flyweight's meta::toString bridge).
		std::string describe() const { return m_type->describe(m_value); }

		// Read the value (compute()) / write it (the adapter, on an edit). set<T>() must use
		// the declared type; get<T>() throws std::bad_any_cast on a type mismatch.
		template <typename T>
		void set(T value);
		template <typename T>
		const T& get() const;
		template <typename T>
		bool holds() const
		{
			return m_value.holds<T>();
		}

		const PortValue& value() const { return m_value; }
		PortValue& value() { return m_value; }

	private:
		friend class Node; // only a Node builds its params
		Param(std::string name, const PortType& type)
			: m_name(std::move(name))
			, m_type(&type)
		{
		}

		std::string m_name;
		const PortType* m_type; // the declared type's shared reflective flyweight
		PortValue m_value;		// seeded with the default at addParam<T>
	};
} // namespace lain::flow

#include "lain/flow/details/param.inl"
