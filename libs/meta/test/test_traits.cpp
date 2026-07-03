// Unit tests for lain::meta type traits. Compile-time checks (STATIC_REQUIRE), so the
// trait declarations below need no definitions.

#include "lain/meta/traits.h"

#include <lain/core/version.h>

#include <catch2/catch_test_macros.hpp>
#include <ostream>
#include <string>

namespace
{
	struct Stringy
	{
		std::string toString() const; // declared only — traits are unevaluated
	};

	struct Streamy
	{
	};
	std::ostream& operator<<(std::ostream&, const Streamy&); // declared only

	struct Plain
	{
	};
} // namespace

TEST_CASE("has_to_string detects a string-returning toString()", "[meta]")
{
	STATIC_REQUIRE(lain::meta::has_to_string_v<lain::core::Version>);
	STATIC_REQUIRE(lain::meta::has_to_string_v<Stringy>);
	STATIC_REQUIRE_FALSE(lain::meta::has_to_string_v<int>);
	STATIC_REQUIRE_FALSE(lain::meta::has_to_string_v<Streamy>);
	STATIC_REQUIRE_FALSE(lain::meta::has_to_string_v<Plain>);
}

TEST_CASE("has_ostream detects a stream insertion operator", "[meta]")
{
	STATIC_REQUIRE(lain::meta::has_ostream_v<int>);
	STATIC_REQUIRE(lain::meta::has_ostream_v<std::string>);
	STATIC_REQUIRE(lain::meta::has_ostream_v<Streamy>);
	STATIC_REQUIRE_FALSE(lain::meta::has_ostream_v<Stringy>);
	STATIC_REQUIRE_FALSE(lain::meta::has_ostream_v<Plain>);
}
