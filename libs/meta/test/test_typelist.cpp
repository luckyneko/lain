// Unit tests for lain::meta::TypeList — the enum-indexed type-table primitive.

#include "lain/meta/typelist.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

using lain::meta::TypeList;
using List = TypeList<std::uint8_t, std::uint16_t, float>;

TEST_CASE("TypeList reports size and type-by-index", "[typelist]")
{
	STATIC_REQUIRE(List::size == 3);
	STATIC_REQUIRE(std::is_same_v<List::at<0>, std::uint8_t>);
	STATIC_REQUIRE(std::is_same_v<List::at<1>, std::uint16_t>);
	STATIC_REQUIRE(std::is_same_v<List::at<2>, float>);
}

TEST_CASE("TypeList::contains and indexOf", "[typelist]")
{
	STATIC_REQUIRE(List::contains<std::uint16_t>);
	STATIC_REQUIRE_FALSE(List::contains<double>);
	STATIC_REQUIRE(List::indexOf<std::uint8_t>() == 0);
	STATIC_REQUIRE(List::indexOf<float>() == 2);
}

TEST_CASE("TypeList::sizeAt gives sizeof by runtime index", "[typelist]")
{
	REQUIRE(List::sizeAt(0) == sizeof(std::uint8_t));
	REQUIRE(List::sizeAt(1) == sizeof(std::uint16_t));
	REQUIRE(List::sizeAt(2) == sizeof(float));
}

TEST_CASE("visitAt dispatches a runtime index to the compile-time type", "[typelist]")
{
	for (std::size_t i = 0; i < List::size; ++i)
	{
		std::size_t seen = 0;
		List::visitAt(i, [&](auto tag)
			{ seen = sizeof(typename decltype(tag)::type); });
		REQUIRE(seen == List::sizeAt(i));
	}
}

TEST_CASE("visitAt selects exactly the matching type", "[typelist]")
{
	bool isFloat = false;
	List::visitAt(2, [&](auto tag)
		{ isFloat = std::is_same_v<typename decltype(tag)::type, float>; });
	REQUIRE(isFloat);
}
