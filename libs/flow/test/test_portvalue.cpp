// Unit tests for lain::flow::PortValue — the type-erased port slot. flow is
// payload-agnostic, so a GPU handle is just another copyable value: the suite
// exercises acm:: handles with null (default-constructed) Texture/Buffer to prove
// they ride through the generic slot, which needs no driver.

#include "lain/flow/portvalue.h"

#include <archimedes/acmBuffer.h>
#include <archimedes/acmTexture.h>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <typeindex>
#include <vector>

using lain::flow::PortValue;

namespace
{
	// A payload whose copies are VISIBLE. It stands in for the real payloads (an image::Image
	// copy is a deep pixel copy), and the scheduler copies a PortValue per edge per run — so a
	// slot that copied its payload would charge that on every edge of every graph, every run.
	struct Tracked
	{
		static int copies;
		Tracked() = default;
		Tracked(const Tracked&) { ++copies; }
		Tracked& operator=(const Tracked&)
		{
			++copies;
			return *this;
		}
		Tracked(Tracked&&) = default;
		Tracked& operator=(Tracked&&) = default;
	};
	int Tracked::copies = 0;
} // namespace

TEST_CASE("a default PortValue is empty", "[portvalue]")
{
	PortValue v;
	REQUIRE(v.empty());
	REQUIRE(v.type() == std::type_index(typeid(void)));
}

TEST_CASE("a CPU value round-trips and reports its type", "[portvalue]")
{
	PortValue v;
	v.set(42);

	REQUIRE_FALSE(v.empty());
	REQUIRE(v.holds<int>());
	REQUIRE_FALSE(v.holds<float>());
	REQUIRE(v.get<int>() == 42);
	REQUIRE(v.type() == std::type_index(typeid(int)));

	SECTION("works for non-trivial types too")
	{
		v.set(std::string{"lain"});
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

TEST_CASE("GPU handles ride through the generic slot", "[portvalue]")
{
	SECTION("texture")
	{
		PortValue v;
		v.set(acm::Texture{}); // a null handle is still a valid acm::Texture payload
		REQUIRE(v.holds<acm::Texture>());
		REQUIRE(v.type() == std::type_index(typeid(acm::Texture)));
		REQUIRE_FALSE(v.get<acm::Texture>().valid());
	}

	SECTION("buffer")
	{
		PortValue v;
		v.set(acm::Buffer{});
		REQUIRE(v.holds<acm::Buffer>());
		REQUIRE(v.type() == std::type_index(typeid(acm::Buffer)));
		REQUIRE_FALSE(v.get<acm::Buffer>().valid());
	}
}

TEST_CASE("sameType compares the payload type", "[portvalue]")
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

	REQUIRE(a.sameType(b));		  // int vs int
	REQUIRE_FALSE(a.sameType(f)); // int vs float
	REQUIRE_FALSE(tex.sameType(buf));
	REQUIRE(tex.sameType(PortValue{tex})); // Texture vs Texture
}

TEST_CASE("clear returns the slot to empty", "[portvalue]")
{
	PortValue v;
	v.set(123);
	v.clear();
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

TEST_CASE("the payload is shared, never copied", "[portvalue]")
{
	Tracked::copies = 0;

	PortValue v;
	v.set(Tracked{}); // moved into the shared allocation — not copied

	PortValue copy = v;
	PortValue assigned;
	assigned = v;
	const std::vector<PortValue> many(8, v);

	REQUIRE(Tracked::copies == 0);
	REQUIRE(copy.holds<Tracked>());
	// Not merely equal — literally the same object, shared by every slot.
	REQUIRE(&copy.get<Tracked>() == &v.get<Tracked>());
	REQUIRE(&assigned.get<Tracked>() == &v.get<Tracked>());
	REQUIRE(&many.front().get<Tracked>() == &v.get<Tracked>());

	SECTION("set REBINDS the slot, so an existing copy's payload is untouched")
	{
		const Tracked* const original = &copy.get<Tracked>();
		v.set(Tracked{});
		REQUIRE(&copy.get<Tracked>() == original); // the copy still reads the old payload
		REQUIRE(&v.get<Tracked>() != original);	   // the overwritten slot points somewhere new
		REQUIRE(Tracked::copies == 0);
	}
}

TEST_CASE("samePayload asks for the same object and not an equal one", "[portvalue]")
{
	// A payload is immutable, so identity is how a viewer asks "has this value changed?" — and a
	// wrong answer either way is invisible: a false "same" leaves a stale picture on screen, a false
	// "different" only costs a re-upload. The sections below each discriminate one plausible wrong
	// implementation (sameType, a control-block compare, an address compare without the type).
	PortValue v;
	v.set<int>(7);

	SECTION("a copy reads the same payload")
	{
		const PortValue copy = v;
		REQUIRE(copy.samePayload(v));
		REQUIRE(v.samePayload(copy));
	}

	SECTION("an equal value set separately is a different payload")
	{
		PortValue equal;
		equal.set<int>(7);
		REQUIRE(equal.sameType(v));			 // same type, same value...
		REQUIRE_FALSE(equal.samePayload(v)); // ...and still not the same object
	}

	SECTION("rebinding a slot makes it a different payload from its earlier copies")
	{
		const PortValue before = v;
		v.set<int>(7);
		REQUIRE_FALSE(v.samePayload(before));
	}

	SECTION("two empty slots are the same, and empty is not a value")
	{
		const PortValue a;
		const PortValue b;
		REQUIRE(a.samePayload(b));
		REQUIRE_FALSE(a.samePayload(v));
		REQUIRE_FALSE(v.samePayload(a));
	}

	SECTION("aliases of one element are the same payload; of two elements, not")
	{
		// Every alias of a collection shares the collection's control block, so a compare of
		// ownership would call two different elements one payload.
		PortValue list;
		list.set(std::vector<int>{1, 2});
		const std::vector<int>& items = list.get<std::vector<int>>();
		const PortValue first = PortValue::alias(list, items[0]);
		const PortValue again = PortValue::alias(list, items[0]);
		const PortValue second = PortValue::alias(list, items[1]);
		REQUIRE(first.samePayload(again));
		REQUIRE_FALSE(first.samePayload(second));
		REQUIRE_FALSE(first.samePayload(list)); // an alias reads a subobject, not its owner
	}

	SECTION("an alias of a first member is not its owner, though it shares the address")
	{
		struct Pair
		{
			int first;
			int second;
		};
		PortValue owner;
		owner.set(Pair{1, 2});
		const PortValue member = PortValue::alias(owner, owner.get<Pair>().first);
		// Standard layout puts the first member at the object's own address, so only the TYPE tells
		// these two apart.
		REQUIRE(static_cast<const void*>(&member.get<int>()) == static_cast<const void*>(&owner.get<Pair>()));
		REQUIRE_FALSE(member.samePayload(owner));
	}
}
