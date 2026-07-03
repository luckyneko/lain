// Tests for lain::core::Factory — string-keyed construction of a Base subclass, with
// each creator's construction context captured in its closure.

#include "lain/core/factory.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace lain::core;

namespace
{
	struct Shape
	{
		virtual ~Shape() = default;
		virtual const char* name() const = 0;
	};

	struct Circle : Shape
	{
		const char* name() const override { return "circle"; }
	};

	struct Square : Shape
	{
		int side;
		explicit Square(int s)
			: side(s)
		{
		}
		const char* name() const override { return "square"; }
	};
} // namespace

TEST_CASE("factory creates a registered type by key", "[core]")
{
	Factory<Shape> f;
	REQUIRE(f.registerType("circle", []
						   { return std::make_unique<Circle>(); }));
	REQUIRE(f.size() == 1);

	const std::unique_ptr<Shape> s = f.create("circle");
	REQUIRE(s != nullptr);
	REQUIRE(std::string(s->name()) == "circle");
}

TEST_CASE("factory create returns nullptr for an unknown key", "[core]")
{
	Factory<Shape> f;
	REQUIRE(f.create("nope") == nullptr);
	REQUIRE_FALSE(f.contains("nope"));
}

TEST_CASE("factory creators capture construction context", "[core]")
{
	Factory<Shape> f;
	const int side = 7;
	f.registerType("square", [side]
				   { return std::make_unique<Square>(side); }); // context in the closure

	const std::unique_ptr<Shape> s = f.create("square");
	REQUIRE(s != nullptr);
	REQUIRE(static_cast<const Square&>(*s).side == 7);
}

TEST_CASE("factory registerType reports add vs replace; keys come back sorted", "[core]")
{
	Factory<Shape> f;
	REQUIRE(f.registerType("square", []
						   { return std::make_unique<Square>(1); })); // new -> true
	REQUIRE(f.registerType("circle", []
						   { return std::make_unique<Circle>(); })); // new -> true
	REQUIRE_FALSE(f.registerType("square", []
								 { return std::make_unique<Square>(2); })); // replace -> false

	REQUIRE(f.keys() == std::vector<std::string>{"circle", "square"});	// alphabetical (ordered map)
	REQUIRE(static_cast<const Square&>(*f.create("square")).side == 2); // the replacement is live
}

TEST_CASE("factory typed helper synthesises the creator and forwards ctor args", "[core]")
{
	Factory<Shape> f;
	REQUIRE(f.registerType<Circle>("circle"));	  // no ctor args
	REQUIRE(f.registerType<Square>("square", 5)); // 5 forwarded to Square(int), captured by copy

	REQUIRE(std::string(f.create("circle")->name()) == "circle");
	REQUIRE(static_cast<const Square&>(*f.create("square")).side == 5);
}
