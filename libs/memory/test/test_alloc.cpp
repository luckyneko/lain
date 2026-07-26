// Unit tests for the lain::memory::alloc/dealloc seam. Pure std, no driver. Covers
// the alignment guarantee, round-trip free, the zero-size contract, and the default.

#include "lain/memory/alloc.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

using namespace lain::memory;

// A pointer's alignment as a byte count: the low bit set of its integer value.
static bool isAlignedTo(const void* p, std::size_t align)
{
	return (reinterpret_cast<std::uintptr_t>(p) % align) == 0;
}

TEST_CASE("alloc honors the requested alignment", "[alloc]")
{
	for (std::size_t align : {alignof(std::max_align_t), std::size_t{16}, std::size_t{64}, std::size_t{256}})
	{
		void* p = alloc(1024, align);
		REQUIRE(p != nullptr);
		REQUIRE(isAlignedTo(p, align));
		dealloc(p, 1024, align);
	}
}

TEST_CASE("alloc defaults to the cache-line alignment", "[alloc]")
{
	void* p = alloc(128);
	REQUIRE(p != nullptr);
	REQUIRE(isAlignedTo(p, defaultAlign));
	REQUIRE(defaultAlign == 64);
	dealloc(p, 128);
}

TEST_CASE("a zero-size allocation is valid and freeable", "[alloc]")
{
	void* p = alloc(0, 64);
	REQUIRE(p != nullptr);
	dealloc(p, 0, 64); // must not crash
}

TEST_CASE("dealloc tolerates a null pointer", "[alloc]")
{
	dealloc(nullptr, 32, 64); // no-op, must not crash
}

TEST_CASE("allocated storage is writable across its whole extent", "[alloc]")
{
	constexpr std::size_t size = 4096;
	auto* bytes = static_cast<std::uint8_t*>(alloc(size, 64));
	REQUIRE(bytes != nullptr);
	for (std::size_t i = 0; i < size; ++i)
		bytes[i] = static_cast<std::uint8_t>(i & 0xFF);
	REQUIRE(bytes[0] == 0);
	REQUIRE(bytes[size - 1] == static_cast<std::uint8_t>((size - 1) & 0xFF));
	dealloc(bytes, size, 64);
}
