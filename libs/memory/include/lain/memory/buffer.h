#pragma once

#include "lain/memory/alloc.h"

#include <cstddef>
#include <string>

namespace lain::memory
{
	// An owned, aligned, fixed-size region of raw bytes.
	//
	// Buffer is defined as much by what it REFUSES as by what it holds: it cannot
	// resize, exposes no per-byte operator[], and guarantees its alignment. Those are
	// exactly the affordances a std::vector<uint8_t> wrongly advertises for a block of
	// bytes — a buffer is not a growable numeric array. Bytes are std::byte (raw
	// storage, not numbers); a caller that needs a typed view reinterprets the pointer.
	//
	// Ownership is unique and move-only (one owner, no accidental copies of a large
	// region). A non-owning slice (BufferView) and a refcounted share (SharedBuffer)
	// are deferred until a real caller needs them.
	//
	// All storage routes through memory::alloc/dealloc, so a future recycling pool is a
	// drop-in behind that seam with no change here. Contents are UNINITIALISED after
	// construction (a file read or decode overwrites them; zeroing every allocation is
	// waste this type declines to pay).
	class Buffer
	{
	public:
		// An empty buffer: no allocation, data() == nullptr, size() == 0.
		Buffer() = default;

		// Allocate `size` bytes aligned to `align`. A zero size stays empty (no
		// allocation). `align` must be a non-zero power of two.
		explicit Buffer(std::size_t size, std::size_t align = defaultAlign);

		~Buffer();

		Buffer(Buffer&& other) noexcept;
		Buffer& operator=(Buffer&& other) noexcept;

		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		std::byte* data() { return m_data; }
		const std::byte* data() const { return m_data; }

		std::size_t size() const { return m_size; }
		std::size_t alignment() const { return m_align; }
		bool empty() const { return m_size == 0; }

		// True when the buffer owns a region (non-empty).
		explicit operator bool() const { return m_data != nullptr; }

		// e.g. "Buffer 4096 bytes (align 64)".
		std::string toString() const;

	private:
		// dealloc the owned region (if any) and return to the empty state.
		void reset() noexcept;

		std::byte* m_data{nullptr};
		std::size_t m_size{0};
		std::size_t m_align{defaultAlign};
	};
} // namespace lain::memory
