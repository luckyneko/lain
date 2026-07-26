// The T <-> Value reflection round-trip — the "provable in isolation, before any graph" test:
// a nested struct with a vector and an optional, plus the leaf/integer edge cases.

#include "lain/data/data.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

using lain::data::fromValue;
using lain::data::toValue;
using lain::data::Value;

namespace demo
{
	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
	};

	void serialize(lain::data::Archive& ar, Vec3& v)
	{
		ar.member("x", v.x).member("y", v.y).member("z", v.z);
	}

	struct Light
	{
		std::string name;
		Vec3 position;
		std::vector<Vec3> samples;
		std::optional<float> intensity;
	};

	void serialize(lain::data::Archive& ar, Light& l)
	{
		ar.member("name", l.name)
			.member("position", l.position) // recurses into Vec3's serialize
			.member("samples", l.samples)	// vector<Vec3> -> Array, each recurses
			.member("intensity", l.intensity);
	}
} // namespace demo

TEST_CASE("leaf and container round-trips", "[serialize]")
{
	REQUIRE(fromValue<int>(toValue(42)) == 42);
	REQUIRE(fromValue<std::string>(toValue(std::string("hi"))) == "hi");
	REQUIRE(fromValue<double>(toValue(3.25)) == 3.25);

	std::vector<int> v{1, 2, 3};
	auto rv = fromValue<std::vector<int>>(toValue(v));
	REQUIRE(rv.has_value());
	REQUIRE(*rv == v);
}

TEST_CASE("nested struct round-trips through Value", "[serialize]")
{
	demo::Light l;
	l.name = "key";
	l.position = {1.0f, 2.0f, 3.0f};
	l.samples = {{4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}};
	l.intensity = 0.5f;

	Value v = toValue(l);
	REQUIRE(v.type() == Value::Type::Object);

	auto r = fromValue<demo::Light>(v);
	REQUIRE(r.has_value());
	REQUIRE(r->name == "key");
	REQUIRE(r->position == l.position);
	REQUIRE(r->samples == l.samples);
	REQUIRE(r->intensity == 0.5f);
}

TEST_CASE("an empty optional omits its key; an absent key reads as empty", "[serialize]")
{
	demo::Light l;
	l.name = "n";
	l.intensity = std::nullopt;

	Value v = toValue(l);
	REQUIRE(v.find("intensity") == nullptr); // omitted, not null

	auto r = fromValue<demo::Light>(v);
	REQUIRE(r.has_value());
	REQUIRE_FALSE(r->intensity.has_value());
}

TEST_CASE("Value-level round-trip is idempotent", "[serialize]")
{
	demo::Light l;
	l.name = "same";
	l.position = {0.5f, 1.5f, 2.5f};
	l.samples = {{1.0f, 1.0f, 1.0f}};
	l.intensity = 9.0f;

	Value once = toValue(l);
	Value twice = toValue(*fromValue<demo::Light>(once));
	REQUIRE(once == twice); // toValue . fromValue . toValue == toValue
}

TEST_CASE("the type is the schema — a mismatch is nullopt, not a coerced value", "[serialize]")
{
	REQUIRE_FALSE(fromValue<int>(Value("not a number")).has_value());
	REQUIRE_FALSE(fromValue<demo::Vec3>(Value(3)).has_value()); // scalar where an object is due

	// range check: 300 does not fit a std::uint8_t
	REQUIRE_FALSE(fromValue<std::uint8_t>(Value(300)).has_value());
	REQUIRE(fromValue<std::uint8_t>(Value(200)) == std::uint8_t{200});
}

namespace demo
{
	enum class Blend
	{
		Normal,
		Add,
		Multiply,
	};

	struct Style
	{
		Blend blend = Blend::Normal;
		std::filesystem::path source;
		std::map<std::string, int> counts;
	};

	void serialize(lain::data::Archive& ar, Style& s)
	{
		ar.member("blend", s.blend).member("source", s.source).member("counts", s.counts);
	}
} // namespace demo

TEST_CASE("an enum round-trips as its name, not its integer", "[serialize]")
{
	REQUIRE(toValue(demo::Blend::Multiply) == Value("Multiply"));
	REQUIRE(fromValue<demo::Blend>(Value("Add")).value() == demo::Blend::Add);
	REQUIRE_FALSE(fromValue<demo::Blend>(Value("Nonsense")).has_value());
}

TEST_CASE("std::filesystem::path and a string-keyed map round-trip", "[serialize]")
{
	demo::Style s;
	s.blend = demo::Blend::Add;
	s.source = std::filesystem::path("assets/tex.png");
	s.counts = {{"a", 1}, {"b", 2}};

	Value v = toValue(s);
	REQUIRE(v.find("source")->asString() != nullptr);
	REQUIRE(*v.find("source")->asString() == "assets/tex.png"); // portable forward slashes
	REQUIRE(v.find("counts")->type() == Value::Type::Object);	// map -> Object

	auto r = fromValue<demo::Style>(v);
	REQUIRE(r.has_value());
	REQUIRE(r->blend == demo::Blend::Add);
	REQUIRE(r->source == std::filesystem::path("assets/tex.png"));
	REQUIRE(r->counts.at("a") == 1);
	REQUIRE(r->counts.at("b") == 2);
}

TEST_CASE("raw bytes round-trip through the Bytes arm, not as an Array", "[serialize]")
{
	std::vector<std::byte> bytes{std::byte{0x00}, std::byte{0xFF}, std::byte{0x7F}};

	Value v = toValue(bytes);
	REQUIRE(v.type() == Value::Type::Bytes);
	REQUIRE(v.asArray() == nullptr); // Bytes is not an Array of ints

	auto r = fromValue<std::vector<std::byte>>(v);
	REQUIRE(r.has_value());
	REQUIRE((*r == bytes)); // parens: don't let Catch stringify std::byte (no StringMaker for it)
}
