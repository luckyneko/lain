// Unit tests for lain::core::Version. Pure std, no driver. Covers construction,
// toString/parse round-trips, malformed input, and triple-only comparison.

#include <lain/core/version.h>

#include <catch2/catch_test_macros.hpp>

using lain::core::Version;

TEST_CASE("construction exposes its fields", "[version]")
{
	const Version v{1, 4, 2, "rc.2", "9e1f3c"};
	REQUIRE(v.major() == 1);
	REQUIRE(v.minor() == 4);
	REQUIRE(v.patch() == 2);
	REQUIRE(v.prerelease() == "rc.2");
	REQUIRE(v.build() == "9e1f3c");

	const Version d{};
	REQUIRE(d.major() == 0);
	REQUIRE(d.prerelease().empty());
	REQUIRE(d.build().empty());
}

TEST_CASE("toString renders the numeric core plus present tags", "[version]")
{
	REQUIRE(Version{1, 2, 3}.toString() == "1.2.3");
	REQUIRE(Version{1, 2, 3, "rc.1"}.toString() == "1.2.3-rc.1");
	REQUIRE(Version{1, 2, 3, "", "abc"}.toString() == "1.2.3+abc");
	REQUIRE(Version{1, 2, 3, "rc.1", "abc"}.toString() == "1.2.3-rc.1+abc");
}

TEST_CASE("parse round-trips well-formed strings", "[version]")
{
	const auto check = [](const char* text)
	{
		const auto v = Version::parse(text);
		REQUIRE(v.has_value());
		REQUIRE(v->toString() == text);
	};
	check("0.0.0");
	check("1.2.3");
	check("10.20.30");
	check("1.2.3-rc.1");
	check("1.2.3+abc");
	check("1.2.3-rc.1+abc");
}

TEST_CASE("parse keeps hyphens inside the tags", "[version]")
{
	// A '-' inside the build tag stays with the build; a '-' inside the pre-release
	// tag is preserved (only the first separator splits).
	const auto a = Version::parse("1.0.0+exp-sha.5114");
	REQUIRE(a.has_value());
	REQUIRE(a->prerelease().empty());
	REQUIRE(a->build() == "exp-sha.5114");

	const auto b = Version::parse("1.0.0-alpha-beta");
	REQUIRE(b.has_value());
	REQUIRE(b->prerelease() == "alpha-beta");
	REQUIRE(b->build().empty());
}

TEST_CASE("parse rejects malformed input", "[version]")
{
	REQUIRE_FALSE(Version::parse("").has_value());
	REQUIRE_FALSE(Version::parse("1").has_value());
	REQUIRE_FALSE(Version::parse("1.2").has_value());
	REQUIRE_FALSE(Version::parse("1.2.3.4").has_value());
	REQUIRE_FALSE(Version::parse("1.x.0").has_value());
	REQUIRE_FALSE(Version::parse("1..3").has_value());
	REQUIRE_FALSE(Version::parse("1.2.3-").has_value());   // empty pre-release
	REQUIRE_FALSE(Version::parse("1.2.3+").has_value());   // empty build
	REQUIRE_FALSE(Version::parse("4294967296.0.0").has_value()); // > UINT32_MAX
}

TEST_CASE("comparison is on the numeric triple, tags ignored", "[version]")
{
	REQUIRE(Version{1, 0, 0} < Version{1, 0, 1});
	REQUIRE(Version{1, 0, 0} < Version{1, 1, 0});
	REQUIRE(Version{1, 9, 9} < Version{2, 0, 0});
	REQUIRE(Version{2, 0, 0} > Version{1, 9, 9});
	REQUIRE(Version{1, 2, 3} <= Version{1, 2, 3});
	REQUIRE(Version{1, 2, 3} >= Version{1, 2, 3});

	// Tags do not participate: differing tags still compare equal.
	REQUIRE(Version{1, 0, 0, "rc.1"} == Version{1, 0, 0});
	REQUIRE(Version{1, 0, 0, "rc.1"} == Version{1, 0, 0, "rc.2", "build"});
	REQUIRE_FALSE(Version{1, 0, 0} != Version{1, 0, 0, "rc.1"});
	REQUIRE(Version{1, 0, 0} != Version{1, 0, 1});
}
