// Unit tests for the COLLECTION capability on PortType (M8 slice 1, ADR-0014) — the bridge a map
// node reads a std::vector payload through without flow core naming a payload type.
//
// Two properties here are load-bearing rather than convenient, and each has a test that fails
// loudly if it regresses:
//   * `at` ALIASES. Splitting a collection across N children must cost N refcount bumps, not N
//     payload copies — an image::Image copy is a deep pixel copy, so a copying split would charge
//     the full payload per element per run, which is exactly what the shared-PortValue slice
//     removed for edges.
//   * an aliased element OUTLIVES the slot it came from. The alias holds the collection's own
//     allocation alive, so a child may still be reading element 3 after the producer's slot has
//     been rebound or cleared. Without the aliasing constructor this is a use-after-free.

#include "lain/flow/porttype.h"
#include "lain/flow/portvalue.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <typeindex>
#include <vector>

using lain::flow::PortType;
using lain::flow::portType;
using lain::flow::PortValue;

namespace
{
	// A payload whose copies are VISIBLE — the same device test_portvalue uses, for the same
	// reason: it stands in for a real payload whose copy is expensive.
	struct Tracked
	{
		static int copies;
		int tag = 0;

		Tracked() = default;
		explicit Tracked(int t)
			: tag(t)
		{
		}
		Tracked(const Tracked& other)
			: tag(other.tag)
		{
			++copies;
		}
		Tracked& operator=(const Tracked& other)
		{
			tag = other.tag;
			++copies;
			return *this;
		}
		Tracked(Tracked&&) = default;
		Tracked& operator=(Tracked&&) = default;
	};
	int Tracked::copies = 0;
} // namespace

TEST_CASE("an ordinary type carries no collection capability", "[collection]")
{
	const PortType& type = portType<int>();

	REQUIRE_FALSE(type.isCollection());
	REQUIRE(type.element == nullptr);
	REQUIRE(type.size == nullptr);
	REQUIRE(type.at == nullptr);
	REQUIRE(type.gather == nullptr);

	// A struct is no more a collection than a scalar: the capability is filled from the type's
	// SHAPE, so nothing can opt in by accident.
	REQUIRE_FALSE(portType<std::string>().isCollection());
	REQUIRE_FALSE(portType<Tracked>().isCollection());
}

TEST_CASE("a collection reports its element type", "[collection]")
{
	const PortType& type = portType<std::vector<int>>();

	REQUIRE(type.isCollection());
	// Not merely a matching type_index — the element field points at the ONE shared descriptor
	// for int, so a map can compare an inner pin's PortType against it by address.
	REQUIRE(type.element == &portType<int>());
	REQUIRE(type.index == std::type_index(typeid(std::vector<int>)));
	REQUIRE(type.element->index == std::type_index(typeid(int)));
}

TEST_CASE("size counts the elements, and an empty slot has none", "[collection]")
{
	const PortType& type = portType<std::vector<int>>();

	PortValue value;
	REQUIRE(type.size(value) == 0); // empty slot: nothing to do, not an error

	value.set(std::vector<int>{4, 5, 6});
	REQUIRE(type.size(value) == 3);

	value.set(std::vector<int>{});
	REQUIRE(type.size(value) == 0); // an empty collection IS a value
}

TEST_CASE("at returns the element by value-identity", "[collection]")
{
	const PortType& type = portType<std::vector<int>>();
	PortValue value;
	value.set(std::vector<int>{7, 8, 9});

	const PortValue second = type.at(value, 1);
	REQUIRE(second.holds<int>());
	REQUIRE(second.get<int>() == 8);
	REQUIRE(second.type() == std::type_index(typeid(int)));

	SECTION("out of range yields empty rather than throwing")
	{
		REQUIRE(type.at(value, 3).empty());
		REQUIRE(type.at(value, 99).empty());
	}

	SECTION("an empty collection slot yields empty")
	{
		PortValue none;
		REQUIRE(type.at(none, 0).empty());
	}
}

TEST_CASE("splitting a collection copies no elements", "[collection]")
{
	// The load-bearing property. Build the collection first, then count from zero: set() MOVES
	// the vector in, so the payload arrives without copying an element.
	std::vector<Tracked> items;
	items.reserve(4);
	for (int i = 0; i < 4; ++i)
		items.emplace_back(i);

	PortValue value;
	value.set(std::move(items));
	Tracked::copies = 0;

	const PortType& type = portType<std::vector<Tracked>>();
	const std::vector<Tracked>& payload = value.get<std::vector<Tracked>>();
	REQUIRE(type.size(value) == 4);

	for (std::size_t i = 0; i < type.size(value); ++i)
	{
		const PortValue element = type.at(value, i);
		REQUIRE(element.holds<Tracked>());
		REQUIRE(element.get<Tracked>().tag == static_cast<int>(i));
		// Not merely equal — literally the element inside the collection's own payload.
		REQUIRE(&element.get<Tracked>() == &payload[i]);
	}

	REQUIRE(Tracked::copies == 0);
}

TEST_CASE("an aliased element outlives the slot it came from", "[collection]")
{
	// The safety property the aliasing constructor buys: a child still reading its element cannot
	// be undercut by the producer recomputing or the slot being cleared.
	const PortType& type = portType<std::vector<int>>();
	PortValue value;
	value.set(std::vector<int>{10, 20, 30});

	const PortValue element = type.at(value, 1);
	REQUIRE(element.get<int>() == 20);

	SECTION("the owner being cleared leaves it readable")
	{
		value.clear();
		REQUIRE(element.holds<int>());
		REQUIRE(element.get<int>() == 20);
	}

	SECTION("the owner being rebound leaves it reading the OLD collection")
	{
		value.set(std::vector<int>{99, 98, 97});
		REQUIRE(element.get<int>() == 20); // not 98
	}
}

TEST_CASE("gather builds one collection from N element slots", "[collection]")
{
	const PortType& type = portType<std::vector<int>>();

	std::vector<PortValue> items(3);
	items[0].set(11);
	items[1].set(22);
	items[2].set(33);

	const PortValue gathered = type.gather(items);
	REQUIRE(gathered.holds<std::vector<int>>());
	const std::vector<int> expected{11, 22, 33};
	REQUIRE(gathered.get<std::vector<int>>() == expected);
}

TEST_CASE("gather of nothing is an empty collection, not an absent value", "[collection]")
{
	// ADR-0014's N == 0 rule, at the capability level: empty in, empty vector out. A caller must
	// be able to tell "the collection has no elements" from "there is no collection".
	const PortType& type = portType<std::vector<int>>();
	const std::vector<PortValue> none;

	const PortValue gathered = type.gather(none);
	REQUIRE_FALSE(gathered.empty());
	REQUIRE(gathered.holds<std::vector<int>>());
	REQUIRE(gathered.get<std::vector<int>>().empty());
}

TEST_CASE("gather reports a hole or a mistyped element as empty", "[collection]")
{
	const PortType& type = portType<std::vector<int>>();
	std::vector<PortValue> items(3);
	items[0].set(1);
	items[2].set(3);

	SECTION("a hole (element 1 never produced a value)")
	{
		REQUIRE(items[1].empty());
		REQUIRE(type.gather(items).empty()); // what the hole MEANS is the map's decision
	}

	SECTION("a wrongly-typed element")
	{
		items[1].set(std::string{"not an int"});
		REQUIRE(type.gather(items).empty());
	}
}

TEST_CASE("split then gather round-trips a collection", "[collection]")
{
	const PortType& type = portType<std::vector<int>>();
	PortValue value;
	value.set(std::vector<int>{2, 4, 6, 8});

	std::vector<PortValue> elements;
	for (std::size_t i = 0; i < type.size(value); ++i)
		elements.push_back(type.at(value, i));

	const PortValue gathered = type.gather(elements);
	REQUIRE(gathered.get<std::vector<int>>() == value.get<std::vector<int>>());
}

TEST_CASE("a nested collection is a collection at every level", "[collection]")
{
	const PortType& outer = portType<std::vector<std::vector<int>>>();

	REQUIRE(outer.isCollection());
	REQUIRE(outer.element == &portType<std::vector<int>>());
	REQUIRE(outer.element->isCollection()); // the element is itself mappable
	REQUIRE(outer.element->element == &portType<int>());

	PortValue value;
	value.set(std::vector<std::vector<int>>{{1, 2}, {3}});
	REQUIRE(outer.size(value) == 2);

	// Descend one level through the aliased inner collection, using the element's own capability.
	const PortValue inner = outer.at(value, 0);
	REQUIRE(inner.holds<std::vector<int>>());
	REQUIRE(outer.element->size(inner) == 2);
	REQUIRE(outer.element->at(inner, 1).get<int>() == 2);
}

TEST_CASE("a proxy container falls back to copying its elements", "[collection]")
{
	// std::vector<bool> packs bits, so operator[] yields a value rather than a reference and there
	// is no element to alias. The capability must still WORK for it — refusing it would be a nasty
	// surprise for whoever registers a per-element flag list — so `at` copies instead.
	const PortType& type = portType<std::vector<bool>>();

	REQUIRE(type.isCollection());
	REQUIRE(type.element == &portType<bool>());

	PortValue value;
	value.set(std::vector<bool>{true, false, true});
	REQUIRE(type.size(value) == 3);

	REQUIRE(type.at(value, 0).get<bool>() == true);
	REQUIRE(type.at(value, 1).get<bool>() == false);
	REQUIRE(type.at(value, 2).get<bool>() == true);

	std::vector<PortValue> items(2);
	items[0].set(false);
	items[1].set(true);
	const std::vector<bool> expected{false, true};
	REQUIRE(type.gather(items).get<std::vector<bool>>() == expected);
}

TEST_CASE("describe still works for a collection type", "[collection]")
{
	// The capability is additive: a collection is describable like any other declared type, so an
	// inspector showing a list-valued pin does not need a special case.
	const PortType& type = portType<std::vector<int>>();

	PortValue empty;
	REQUIRE(type.describe(empty) == "(empty)");

	PortValue value;
	value.set(std::vector<int>{1, 2});
	REQUIRE_FALSE(type.describe(value).empty());
}
