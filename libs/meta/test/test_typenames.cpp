// Unit tests for lain::meta::typeName / typeNameShort (owned signature parse). Pure
// std, no driver.
// Names from compiler intrinsics vary in incidentals across compilers, so assert
// stable cases: a builtin, an exact short name, and a substring of the qualified name.

#include <lain/meta/typenames.h>

#include <lain/core/version.h>

#include <catch2/catch_test_macros.hpp>
#include <string>

namespace
{
	struct Widget
	{
	};
}

TEST_CASE("typeName of a builtin is stable", "[meta]")
{
	REQUIRE(std::string(lain::meta::typeName<int>()) == "int");
}

TEST_CASE("typeNameShort drops the namespace", "[meta]")
{
	REQUIRE(std::string(lain::meta::typeNameShort<lain::core::Version>()) == "Version");
	REQUIRE(std::string(lain::meta::typeNameShort<Widget>()) == "Widget");
}

TEST_CASE("typeName keeps the qualification, without the class/struct keyword", "[meta]")
{
	// The leading keyword some toolchains prefix ("class …") is stripped.
	REQUIRE(std::string(lain::meta::typeName<lain::core::Version>()) == "lain::core::Version");
	REQUIRE(std::string(lain::meta::typeName<Widget>()).rfind("class ", 0) != 0);
	REQUIRE(std::string(lain::meta::typeName<Widget>()).rfind("struct ", 0) != 0);
}
