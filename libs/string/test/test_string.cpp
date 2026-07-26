// Unit tests for lain::string. format() over fmt, and the trait-gated formatter that
// renders a lain type (Version) through its toString() — including that fmt's format
// specs still apply via the inherited std::string formatter.

#include "lain/string/format.h"

#include <lain/core/version.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using lain::core::Version;
using lain::string::format;

TEST_CASE("format builds strings from a checked format + args", "[string]")
{
	REQUIRE(format("{} {}", 1, "a") == "1 a");
	REQUIRE(format("{:.2f}", 3.14159) == "3.14");
	REQUIRE(format("no placeholders") == "no placeholders");
	REQUIRE(format("{0}-{0}", 7) == "7-7");
}

TEST_CASE("a lain type renders via its toString()", "[string]")
{
	REQUIRE(format("{}", Version{1, 2, 3}) == "1.2.3");
	REQUIRE(format("v{}", Version{1, 2, 3, "rc.1"}) == "v1.2.3-rc.1");
	REQUIRE(format("{} then {}", Version{0, 1, 0}, Version{2, 0, 0}) == "0.1.0 then 2.0.0");
}

TEST_CASE("format specs apply to a lain type via the inherited formatter", "[string]")
{
	// The formatter derives from fmt::formatter<std::string>, so width / fill / align
	// specs are honoured on the toString() result.
	REQUIRE(format("[{:>8}]", Version{1, 2, 3}) == "[   1.2.3]");
	REQUIRE(format("[{:*<8}]", Version{1, 2, 3}) == "[1.2.3***]");
}
