#pragma once

// The std::hash specialisation for core::Uuid. Split out of uuid.h for the reason
// details/colormath.inl is split out of colormath.h: the header should read as the type's
// interface, and bit-folding is implementation. Its EXISTENCE is interface, which is why uuid.h
// says so where this is included — a specialisation cannot be usefully forward-declared, so it
// must travel with the header either way.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

// Folds the 16 bytes as two 64-bit halves — the bits are already well distributed (v7's tail is
// random), so no avalanche step is needed.
template <>
struct std::hash<lain::core::Uuid>
{
	std::size_t operator()(const lain::core::Uuid& id) const noexcept
	{
		const std::array<std::uint8_t, 16>& b = id.bytes();
		std::uint64_t high = 0;
		std::uint64_t low = 0;
		for (std::size_t i = 0; i < 8; ++i)
		{
			high = (high << 8) | b[i];
			low = (low << 8) | b[i + 8];
		}
		return static_cast<std::size_t>(high ^ (low + 0x9e3779b97f4a7c15ULL + (high << 6) + (high >> 2)));
	}
};
