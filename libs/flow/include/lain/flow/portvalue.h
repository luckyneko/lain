#pragma once

#include <any>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <variant>

#include <archimedes/acmBuffer.h>
#include <archimedes/acmTexture.h>

namespace lain::flow
{
	// The kind of payload a PortValue currently holds. A closed set, so the viewer
	// and scheduler dispatch on it via the variant index — no RTTI on the hot path.
	enum class PortKind
	{
		Empty,
		Cpu,
		Texture,
		Buffer,
	};

	// A persistent, type-erased slot holding one port's value: a CPU payload (any
	// copyable type) or a GPU resource (acm::Texture / acm::Buffer). A port owns its
	// PortValue and overwrites it in place on recompute; it is never moved or
	// consumed downstream, which is what keeps every stage inspectable after a run.
	//
	// The kind tag is the variant index (RTTI-free). Only the CPU arm is erased:
	// it stays open (std::any) for now — WORK.md's "closed variant of known types"
	// is deferred until real nodes pin that set down, and is swappable behind this
	// interface.
	class PortValue
	{
	public:
		PortValue() = default; // Empty

		// --- CPU payload ---

		// Overwrite the slot with a CPU value of any copyable type.
		template <typename T>
		void set(T value);

		// True when the slot currently holds a CPU value of exactly T.
		template <typename T>
		bool holds() const;

		// The held CPU value as T. Precondition: holds<T>(); throws std::bad_any_cast
		// (or std::bad_variant_access if not a CPU value) otherwise.
		template <typename T>
		const T& get() const;

		// --- GPU payload ---

		void set(acm::Texture texture);
		void set(acm::Buffer buffer);

		// Precondition: kind() == Texture / Buffer respectively.
		const acm::Texture& texture() const;
		const acm::Buffer& buffer() const;

		// --- introspection ---

		PortKind kind() const;
		bool empty() const { return kind() == PortKind::Empty; }

		// type_index of the held payload: the CPU value's type, or
		// typeid(acm::Texture) / typeid(acm::Buffer) for the GPU kinds, or
		// typeid(void) when empty. Drives connection type-checking.
		std::type_index type() const;

		// Two values may connect when they carry the same kind and the same type.
		bool sameType(const PortValue& other) const;

		// Drop the payload, returning the slot to Empty.
		void clear();

	private:
		std::variant<std::monostate, std::any, acm::Texture, acm::Buffer> m_value;
	};
}

#include <lain/flow/details/portvalue.inl>
