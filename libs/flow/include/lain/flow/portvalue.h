#pragma once

#include <any>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace lain::flow
{
	// A persistent, type-erased slot holding one port's value: any copyable
	// payload. A CPU value, or a GPU handle (acm::Texture / acm::Buffer are just
	// copyable shared_ptr handles), rides here with no special-casing. A port owns
	// its PortValue and overwrites it in place on recompute; it is never moved or
	// consumed downstream, which is what keeps every stage inspectable after a run.
	//
	// flow is payload-agnostic: PortValue names no GPU types and depends on nothing.
	// A consumer that cares whether a slot holds, say, an acm::Texture compares
	// type() against typeid(acm::Texture) (the viewer does exactly this to dispatch
	// a GPU output to a thumbnail).
	class PortValue
	{
	public:
		PortValue() = default; // empty

		// Overwrite the slot with a value of any copyable type.
		template <typename T>
		void set(T value);

		// True when the slot currently holds a value of exactly T.
		template <typename T>
		bool holds() const;

		// The held value as T. Precondition: holds<T>(); throws std::bad_any_cast
		// otherwise.
		template <typename T>
		const T& get() const;

		bool empty() const { return !m_value.has_value(); }

		// type_index of the held payload, or typeid(void) when empty. Drives
		// connection type-checking.
		std::type_index type() const;

		// Two values may connect when they carry the same payload type.
		bool sameType(const PortValue& other) const;

		// Drop the payload, returning the slot to empty.
		void clear();

	private:
		std::any m_value;
	};
} // namespace lain::flow

#include "lain/flow/details/portvalue.inl"
