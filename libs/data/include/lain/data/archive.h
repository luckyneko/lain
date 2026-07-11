#pragma once

#include "lain/data/value.h"

#include <string_view>

// lain::data::Archive — the direction-agnostic visitor a type's serialize() writes against.
// Concrete (not a template) so `serialize(Archive&, T&)` is one plain signature; member<T> is
// the templated per-field binder. Constructed by toValue / fromValue (data.h) — a serialize
// body only ever calls member().
//
// A serialize() author includes THIS header (they need only the Archive); a caller of the
// toValue / fromValue facade includes data.h. See data.h for the serialize() contract.
namespace lain::data
{
	class Archive
	{
	public:
		enum class Mode
		{
			Save, // reading fields out of T, building a Value Object
			Load, // reading fields out of a Value Object, into T
		};

		// Built by toValue (Save, over the Object being built) / fromValue (Load, over the
		// Object being read). Public so the facade can construct one; not meant for direct use.
		Archive(Mode mode, Value* out) noexcept : m_mode(mode), m_out(out) {}
		Archive(Mode mode, const Value* in) noexcept : m_mode(mode), m_in(in) {}

		Mode mode() const noexcept { return m_mode; }
		bool saving() const noexcept { return m_mode == Mode::Save; }
		bool loading() const noexcept { return m_mode == Mode::Load; }

		// Bind one named field, both directions. On Save: writes key -> the field's Value. On
		// Load: reads the key's Value into the field (absent key → left at its default —
		// tolerant). A std::optional field is special-cased: on Save an empty optional OMITS the
		// key (not a null), on Load an absent key resets it. Returns *this so members chain.
		template <typename T>
		Archive& member(std::string_view key, T& value);

	private:
		Mode m_mode;
		Value* m_out = nullptr;		 // Save target (an Object)
		const Value* m_in = nullptr; // Load source (an Object)
	};
} // namespace lain::data

#include "lain/data/details/archive.inl"
