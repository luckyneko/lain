// Unit tests for lain::meta::typeName / typeNameShort (nameof). Pure std, no driver.
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

TEST_CASE("typeName keeps the qualification", "[meta]")
{
	const std::string full(lain::meta::typeName<lain::core::Version>());
	REQUIRE(full.find("lain::core::Version") != std::string::npos);
}
