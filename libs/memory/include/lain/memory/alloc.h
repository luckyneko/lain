#pragma once

#include <cstddef>

namespace lain::memory
{
	// The default alignment for a Buffer: a 64-byte cache line, which also covers the
	// widest common SIMD load (AVX-512). Buffers are aligned by design — see Buffer.
	inline constexpr std::size_t defaultAlign = 64;

	// The allocation seam every Buffer routes through — a service-shaped pair of free
	// functions (the lain::log shape) fronting the actual allocator. Today the backing
	// is the C++17 aligned global operator new/delete; a recycling pool drops in behind
	// these two functions later with no change to any caller, because Buffer names only
	// alloc/dealloc and never the backend.
	//
	// The pair is sized and aligned on purpose: a pool reclaims by size class and the
	// system's sized-delete is faster, and Buffer knows both its size and alignment, so
	// it passes them for free. `align` must be a non-zero power of two (asserted).

	// Allocate `size` bytes aligned to `align`. Contents are uninitialised. A zero size
	// returns a valid, non-null pointer (freeable via dealloc with the same size/align).
	[[nodiscard]] void* alloc(std::size_t size, std::size_t align = defaultAlign);

	// Free a region obtained from alloc, given the same `size` and `align` it was made
	// with. `ptr` may be null (a no-op).
	void dealloc(void* ptr, std::size_t size, std::size_t align = defaultAlign) noexcept;
} // namespace lain::memory
