// Unit tests for lain::memory::Buffer. Pure std, no driver. Covers the empty state,
// sized construction + alignment, move semantics (ownership transfer), the zero-size
// contract, byte access, and toString.

#include "lain/memory/buffer.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>

using lain::memory::Buffer;
using lain::memory::defaultAlign;

static bool isAlignedTo(const void* p, std::size_t align)
{
	return (reinterpret_cast<std::uintptr_t>(p) % align) == 0;
}

TEST_CASE("a default buffer is empty and owns nothing", "[buffer]")
{
	const Buffer b{};
	REQUIRE(b.size() == 0);
	REQUIRE(b.empty());
	REQUIRE(b.data() == nullptr);
	REQUIRE_FALSE(static_cast<bool>(b));
}

TEST_CASE("a sized buffer allocates aligned storage", "[buffer]")
{
	Buffer b{1024};
	REQUIRE(b.size() == 1024);
	REQUIRE_FALSE(b.empty());
	REQUIRE(b.data() != nullptr);
	REQUIRE(static_cast<bool>(b));
	REQUIRE(b.alignment() == defaultAlign);
	REQUIRE(isAlignedTo(b.data(), defaultAlign));
}

TEST_CASE("a buffer honors a custom alignment", "[buffer]")
{
	Buffer b{100, 256};
	REQUIRE(b.alignment() == 256);
	REQUIRE(isAlignedTo(b.data(), 256));
}

TEST_CASE("a zero-size buffer stays empty without allocating", "[buffer]")
{
	Buffer b{0};
	REQUIRE(b.size() == 0);
	REQUIRE(b.empty());
	REQUIRE(b.data() == nullptr);
}

TEST_CASE("bytes are writable across the whole extent", "[buffer]")
{
	Buffer b{512, 64};
	std::byte* p = b.data();
	for (std::size_t i = 0; i < b.size(); ++i)
		p[i] = static_cast<std::byte>(i & 0xFF);
	REQUIRE(std::to_integer<int>(p[0]) == 0);
	REQUIRE(std::to_integer<int>(p[511]) == (511 & 0xFF));
}

TEST_CASE("move construction transfers ownership and empties the source", "[buffer]")
{
	Buffer src{256, 128};
	std::byte* const region = src.data();

	Buffer dst{std::move(src)};
	REQUIRE(dst.data() == region);
	REQUIRE(dst.size() == 256);
	REQUIRE(dst.alignment() == 128);

	REQUIRE(src.data() == nullptr); // NOLINT(bugprone-use-after-move) — asserting the moved-from state
	REQUIRE(src.size() == 0);
	REQUIRE(src.empty());
}

TEST_CASE("move assignment frees the target then transfers", "[buffer]")
{
	Buffer dst{64};
	Buffer src{256, 128};
	std::byte* const region = src.data();

	dst = std::move(src);
	REQUIRE(dst.data() == region);
	REQUIRE(dst.size() == 256);
	REQUIRE(dst.alignment() == 128);
	REQUIRE(src.data() == nullptr); // NOLINT(bugprone-use-after-move)
	REQUIRE(src.size() == 0);
}

TEST_CASE("move assignment is self-safe", "[buffer]")
{
	Buffer b{128, 64};
	std::byte* const region = b.data();
	// Launder through a reference so the assignment isn't a syntactic self-move
	// (which -Wself-move would reject) — the runtime path is still self-assignment.
	Buffer& alias = b;
	b = std::move(alias);
	REQUIRE(b.data() == region);
	REQUIRE(b.size() == 128);
}

TEST_CASE("toString reports size and alignment", "[buffer]")
{
	REQUIRE(Buffer{4096, 64}.toString() == "Buffer 4096 bytes (align 64)");
	REQUIRE(Buffer{}.toString() == "Buffer 0 bytes (align 64)");
}
