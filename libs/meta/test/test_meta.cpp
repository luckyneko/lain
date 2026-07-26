// Unit tests for lain::meta::enums reflection. Pure std, no driver.

#include "lain/meta/enums.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

namespace
{
	enum class Color
	{
		Red,
		Green,
		Blue,
	};
}

namespace enums = lain::meta::enums;

TEST_CASE("enum names and count", "[meta]")
{
	// Wrap string_view results in std::string: Catch2's prebuilt lib has no
	// StringMaker<string_view>, so comparing string_view directly fails to link.
	REQUIRE(enums::count<Color>() == 3);
	REQUIRE(std::string(enums::name(Color::Red)) == "Red");
	REQUIRE(std::string(enums::name(Color::Green)) == "Green");
	REQUIRE(std::string(enums::name(Color::Blue)) == "Blue");
}

TEST_CASE("fromString round-trips, case rules respected", "[meta]")
{
	REQUIRE(enums::fromString<Color>("Green") == Color::Green);
	REQUIRE_FALSE(enums::fromString<Color>("Magenta").has_value());

	// Case-sensitive by default; opt into case-insensitive.
	REQUIRE_FALSE(enums::fromString<Color>("green").has_value());
	REQUIRE(enums::fromString<Color>("green", /*caseInsensitive*/ true) == Color::Green);
}

TEST_CASE("values, names, and entries are in declaration order", "[meta]")
{
	const auto values = enums::values<Color>();
	REQUIRE(values.size() == 3);
	REQUIRE(values[0] == Color::Red);
	REQUIRE(values[2] == Color::Blue);

	const auto names = enums::names<Color>();
	REQUIRE(std::string(names[1]) == "Green");

	const auto entries = enums::entries<Color>();
	REQUIRE(entries.size() == 3);
	REQUIRE(entries[0].first == Color::Red);
	REQUIRE(std::string(entries[0].second) == "Red");
}

TEST_CASE("nameValueMap keys names to enumerators", "[meta]")
{
	const std::map<std::string, Color> map = enums::nameValueMap<Color>();
	REQUIRE(map.size() == 3);
	REQUIRE(map.at("Red") == Color::Red);
	REQUIRE(map.at("Green") == Color::Green);
	REQUIRE(map.at("Blue") == Color::Blue);
	REQUIRE(map.count("Magenta") == 0);
}
