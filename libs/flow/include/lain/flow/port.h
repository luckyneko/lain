#pragma once

#include <lain/flow/portvalue.h>

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

		// The declared type, fixed at declaration — what connect() type-checks
		// against (independent of whether a value has been produced yet).
		std::type_index type() const { return m_type; }

		// A human-readable name for the declared type, captured from
		// lain::meta::typeName<T>() at declaration (a string_view into static storage).
		// For display/debug — the inspector labels pins with it.
		std::string_view typeName() const { return m_typeName; }

		// True once a value has been produced into this port (i.e. not empty).
		bool ready() const { return !m_value.empty(); }

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
		friend class Node; // only a Node builds its ports
		Port(std::string name, Direction dir, std::type_index type, std::string_view typeName)
			: m_name(std::move(name))
			, m_dir(dir)
			, m_type(type)
			, m_typeName(typeName)
		{
		}

		std::string m_name;
		Direction m_dir;
		std::type_index m_type;
		std::string_view m_typeName;
		PortValue m_value;
	};
} // namespace lain::flow

#include <lain/flow/details/port.inl>
