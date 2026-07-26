// Unit tests for lain::meta::toString — the best-effort, fmt-free stringify: bool ->
// true/false, a member toString(), an ostream operator, else the type name.

#include "lain/meta/tostring.h"

#include <catch2/catch_test_macros.hpp>

#include <ostream>
#include <string>

namespace tostring_test
{
	struct Stamped
	{
		std::string toString() const { return "stamped!"; }
	};

	struct Streamable
	{
		int n;
	};
	inline std::ostream& operator<<(std::ostream& os, const Streamable& s) { return os << "S(" << s.n << ")"; }

	struct Opaque
	{
		int x;
	}; // neither toString nor ostream -> type-name fallback
} // namespace tostring_test

using namespace lain::meta;
using namespace tostring_test;

TEST_CASE("toString renders scalars and strings via ostream", "[tostring]")
{
	REQUIRE(toString(42) == "42");
	REQUIRE(toString(1.5f) == "1.5");
	REQUIRE(toString(std::string("hi")) == "hi");
}

TEST_CASE("toString renders bool as true/false", "[tostring]")
{
	REQUIRE(toString(true) == "true");
	REQUIRE(toString(false) == "false");
}

TEST_CASE("toString prefers a member toString()", "[tostring]")
{
	REQUIRE(toString(Stamped{}) == "stamped!");
}

TEST_CASE("toString uses an ostream operator when there is no toString()", "[tostring]")
{
	REQUIRE(toString(Streamable{7}) == "S(7)");
}

TEST_CASE("toString falls back to the type name for an opaque type", "[tostring]")
{
	const std::string s = toString(Opaque{});
	REQUIRE(s.front() == '<');
	REQUIRE(s.back() == '>');
	REQUIRE(s.find("Opaque") != std::string::npos);
}
