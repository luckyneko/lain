#pragma once

#include "lain/flow/porttype.h" // PortType — the per-type reflective flyweight
#include "lain/flow/portvalue.h"
#include "lain/flow/types.h" // PortId

#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

namespace lain::flow
{
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

	private:
		friend class Node; // only a Node builds its ports (and assigns the id)
		Port(std::string name, Direction dir, const PortType& type, PortId id)
			: m_name(std::move(name))
			, m_dir(dir)
			, m_type(&type)
			, m_id(id)
		{
		}

		std::string m_name;
		Direction m_dir;
		const PortType* m_type; // the declared type's shared reflective flyweight
		PortId m_id;			// stable identity within the owning node
		PortValue m_value;
	};
} // namespace lain::flow

#include "lain/flow/details/port.inl"
