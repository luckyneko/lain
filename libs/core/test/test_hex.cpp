// Unit tests for lain::core's hex helpers. Uuid's canonical form is built on them, so test_uuid's
// formatting cases are their regression test too.

#include "lain/core/hex.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace lain::core;

TEST_CASE("hexDigits writes two lower-case digits per byte, high nibble first", "[hex]")
{
	const std::uint8_t bytes[] = {0x00, 0x0f, 0xa5, 0xf0, 0xff};
	REQUIRE(hexDigits(bytes, 5) == "000fa5f0ff");
	REQUIRE(hexDigits(bytes, 0).empty());
}

TEST_CASE("hexDigitValue reads either case and refuses everything else", "[hex]")
{
	REQUIRE(hexDigitValue('0') == 0);
	REQUIRE(hexDigitValue('9') == 9);
	REQUIRE(hexDigitValue('a') == 10);
	REQUIRE(hexDigitValue('F') == 15);
	// The neighbours of each accepted run, where an off-by-one would land.
	for (const char c : {'/', ':', '@', 'G', '`', 'g', ' ', '-'})
		REQUIRE(hexDigitValue(c) == -1);
}
