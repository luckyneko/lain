#pragma once

#include "lain/data/value.h"

#include <string_view>

// lain::data — the reflection CUSTOMIZATION SURFACE: the two hooks a type-author touches to
// control how it maps to/from a Value. Include this header to WRITE the mapping; include data.h to
// CALL the toValue / fromValue facade.
//
//   * Archive — the direction-agnostic visitor a member-based serialize() writes against.
//     Concrete (not a template) so `serialize(Archive&, T&)` is one plain signature; member<T> is
//     the templated per-field binder. Constructed by toValue / fromValue — a serialize body only
//     ever calls member().
//   * VariantArm — the hook for a std::variant's per-arm discriminator (see below).
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
		Archive(Mode mode, Value* out) noexcept
			: m_mode(mode)
			, m_out(out)
		{
		}
		Archive(Mode mode, const Value* in) noexcept
			: m_mode(mode)
			, m_in(in)
		{
		}

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

	// The tagged-variant customization hook. A user provides `variantArmKey(VariantArm<T>)` — ADL-found
	// via T's namespace, or written with LAIN_SERIALIZE_VARIANT_ARM(T, "key") — returning the stable
	// string key for variant arm T. Deliberately not meta::typeName (display-only, unstable). See data.h.
	template <typename T>
	struct VariantArm
	{
	};
} // namespace lain::data

#include "lain/data/details/archive.inl"
