// Unit tests for lain::flow::PortValue — the dual CPU/GPU port slot. GPU kinds
// are exercised with null acm:: handles (a default Texture/Buffer is a valid
// empty handle), so the whole suite runs without a driver.

#include <string>
#include <typeindex>

#include <catch2/catch_test_macros.hpp>

#include <archimedes/acmBuffer.h>
#include <archimedes/acmTexture.h>
#include <lain/flow/portvalue.h>

using lain::flow::PortKind;
using lain::flow::PortValue;

TEST_CASE("a default PortValue is empty", "[portvalue]")
{
	PortValue v;
	REQUIRE(v.kind() == PortKind::Empty);
	REQUIRE(v.empty());
	REQUIRE(v.type() == std::type_index(typeid(void)));
}

TEST_CASE("a CPU value round-trips and reports its type", "[portvalue]")
{
	PortValue v;
	v.set(42);

	REQUIRE(v.kind() == PortKind::Cpu);
	REQUIRE_FALSE(v.empty());
	REQUIRE(v.holds<int>());
	REQUIRE_FALSE(v.holds<float>());
	REQUIRE(v.get<int>() == 42);
	REQUIRE(v.type() == std::type_index(typeid(int)));

	SECTION("works for non-trivial types too")
	{
		v.set(std::string{ "lain" });
		REQUIRE(v.holds<std::string>());
		REQUIRE(v.get<std::string>() == "lain");
	}
}

TEST_CASE("overwriting reuses the one persistent slot", "[portvalue]")
{
	PortValue v;
	v.set(1);
	REQUIRE(v.holds<int>());

	v.set(2.5f); // recompute overwrites in place
	REQUIRE_FALSE(v.holds<int>());
	REQUIRE(v.holds<float>());
	REQUIRE(v.get<float>() == 2.5f);
	REQUIRE(v.type() == std::type_index(typeid(float)));
}

TEST_CASE("GPU resources select the Texture / Buffer kind", "[portvalue]")
{
	SECTION("texture")
	{
		PortValue v;
		v.set(acm::Texture{}); // null handle is still a Texture-kind payload
		REQUIRE(v.kind() == PortKind::Texture);
		REQUIRE(v.type() == std::type_index(typeid(acm::Texture)));
		REQUIRE_FALSE(v.texture().valid());
	}

	SECTION("buffer")
	{
		PortValue v;
		v.set(acm::Buffer{});
		REQUIRE(v.kind() == PortKind::Buffer);
		REQUIRE(v.type() == std::type_index(typeid(acm::Buffer)));
		REQUIRE_FALSE(v.buffer().valid());
	}
}

TEST_CASE("sameType compares kind and payload type", "[portvalue]")
{
	PortValue a;
	a.set(7);
	PortValue b;
	b.set(8);
	PortValue f;
	f.set(1.0f);
	PortValue tex;
	tex.set(acm::Texture{});
	PortValue buf;
	buf.set(acm::Buffer{});

	REQUIRE(a.sameType(b));        // int vs int
	REQUIRE_FALSE(a.sameType(f));  // int vs float
	REQUIRE_FALSE(tex.sameType(buf));
	REQUIRE(tex.sameType(PortValue{ tex })); // Texture vs Texture
}

TEST_CASE("clear returns the slot to empty", "[portvalue]")
{
	PortValue v;
	v.set(123);
	v.clear();
	REQUIRE(v.kind() == PortKind::Empty);
	REQUIRE(v.empty());
}

TEST_CASE("PortValue copies carry the payload", "[portvalue]")
{
	PortValue v;
	v.set(99);

	PortValue copy = v;
	REQUIRE(copy.holds<int>());
	REQUIRE(copy.get<int>() == 99);

	v.set(100); // mutating the original leaves the copy untouched
	REQUIRE(copy.get<int>() == 99);
}
