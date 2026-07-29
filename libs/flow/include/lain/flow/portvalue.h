#pragma once

#include <memory>
#include <typeindex>
#include <typeinfo>

namespace lain::flow
{
	// A persistent, type-erased slot holding one port's value: any payload at all. A CPU
	// value, or a GPU handle (acm::Texture / acm::Buffer are just copyable shared_ptr
	// handles), rides here with no special-casing. A port owns its PortValue and overwrites
	// it in place on recompute; it is never moved or consumed downstream, which is what keeps
	// every stage inspectable after a run.
	//
	// The payload is SHARED AND IMMUTABLE: set() moves the value into a fresh shared const
	// allocation and the slot holds a pointer to it, so copying a PortValue is a refcount
	// bump and never a payload copy. That is load-bearing, not an optimisation detail — the
	// scheduler copies a PortValue per EDGE per RUN (Scheduler::populateInputs), and payloads
	// are large (an image::Image copy is a deep pixel copy), so a copying slot charges every
	// edge of every graph the full payload on every run.
	//
	// Immutability is what makes the sharing safe, and it costs nothing: get() has always
	// handed out a const reference, and the threading contract already says a node writes
	// only its own outputs rather than mutating an input in place. A node that genuinely
	// wants to modify a value copies it out (`image::Image src = input(i).get<image::Image>()`)
	// exactly as before. set() rebinds the slot rather than writing through the pointer, so a
	// recompute never mutates a payload another PortValue is still reading.
	//
	// flow is payload-agnostic: PortValue names no GPU types and depends on nothing.
	// A consumer that cares whether a slot holds, say, an acm::Texture compares
	// type() against typeid(acm::Texture) (the viewer does exactly this to dispatch
	// a GPU output to a thumbnail).
	class PortValue
	{
	public:
		PortValue() = default; // empty

		// Overwrite the slot with a value of any type. The value is moved into a fresh shared
		// allocation, so any copy of this PortValue taken earlier keeps the OLD payload alive
		// and unchanged.
		template <typename T>
		void set(T value);

		// True when the slot currently holds a value of exactly T.
		template <typename T>
		bool holds() const;

		// The held value as T. Precondition: holds<T>(); throws std::bad_any_cast
		// otherwise. The reference is INTO the shared payload, so it stays valid as long as
		// any PortValue holding that payload lives.
		template <typename T>
		const T& get() const;

		bool empty() const { return m_value == nullptr; }

		// type_index of the held payload, or typeid(void) when empty. Drives
		// connection type-checking.
		std::type_index type() const { return m_type; }

		// Two values may connect when they carry the same payload type.
		bool sameType(const PortValue& other) const;

		// Drop the payload, returning the slot to empty.
		void clear();

	private:
		// The payload, type-erased behind a shared_ptr<const void> — which keeps T's deleter
		// (so destruction is correct) but loses T's identity, hence the type_index beside it.
		// const in the pointer type is what makes the sharing safe by construction.
		std::shared_ptr<const void> m_value;
		std::type_index m_type{typeid(void)};
	};
} // namespace lain::flow

#include "lain/flow/details/portvalue.inl"
