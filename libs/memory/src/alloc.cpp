#include "lain/memory/alloc.h"

#include <cassert>
#include <new>

namespace lain::memory
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// A non-zero power of two is the requirement for an over-aligned new/delete. Its only
	// callers are the asserts below, so NDEBUG compiles every use away and leaves the
	// definition unused — which -Werror,-Wunused-function then rejects. maybe_unused states
	// that rather than dropping the check or hiding it behind an #ifdef.
	[[maybe_unused]] static bool isPowerOfTwo(std::size_t v)
	{
		return v != 0 && (v & (v - 1)) == 0;
	}

	void* alloc(std::size_t size, std::size_t align)
	{
		assert(isPowerOfTwo(align) && "alignment must be a non-zero power of two");
		// C++17 aligned global operator new. Throws std::bad_alloc on failure (never
		// returns null), matching the standard allocator contract.
		return ::operator new(size, std::align_val_t{align});
	}

	void dealloc(void* ptr, std::size_t size, std::size_t align) noexcept
	{
		assert(isPowerOfTwo(align) && "alignment must be a non-zero power of two");
		// Sized, aligned delete — pairs with the aligned new above. A null ptr is a
		// no-op, as for plain operator delete.
		::operator delete(ptr, size, std::align_val_t{align});
	}
} // namespace lain::memory
