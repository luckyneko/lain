// The strict read: fromValue tolerates what a newer build might have written, and fromValueStrict
// reports what that tolerance hid in a document a person typed.

#include "lain/data/data.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

using lain::data::fromValue;
using lain::data::fromValueStrict;
using lain::data::toValue;
using lain::data::Value;

namespace strictdemo
{
	enum class Shade
	{
		Light,
		Dark,
	};

	struct Circle
	{
		double r = 0;
	};
	struct Square
	{
		double side = 0;
	};
	LAIN_SERIALIZE(Circle, r)
	LAIN_SERIALIZE(Square, side)
	LAIN_SERIALIZE_VARIANT_ARM(Circle, "circle")
	LAIN_SERIALIZE_VARIANT_ARM(Square, "square")

	struct Record
	{
		std::string name;
		int count = 0;
		float scale = 1.0f;
		Shade shade = Shade::Light;
		std::optional<double> weight;
		std::vector<int> items;
		std::variant<Circle, Square> shape;
	};
	LAIN_SERIALIZE(Record, name, count, scale, shade, weight, items, shape)
} // namespace strictdemo

namespace
{
	Value good()
	{
		strictdemo::Record record;
		record.name = "a";
		record.count = 3;
		record.scale = 0.75f;
		record.shade = strictdemo::Shade::Dark;
		record.weight = 2.5;
		record.items = {1, 2};
		record.shape = strictdemo::Square{2.0};
		return toValue(record);
	}

	Value with(const char* key, Value replacement)
	{
		Value document = good();
		document.set(key, std::move(replacement));
		return document;
	}

	Value without(const char* key)
	{
		// Held, not iterated as good().asObject(): a range-for keeps the reference alive, not the
		// temporary it points into.
		const Value source = good();
		Value document = Value::object();
		for (const auto& [name, value] : *source.asObject())
		{
			if (name != key)
				document.set(name, value);
		}
		return document;
	}

	std::vector<std::string> problemsOf(const Value& document)
	{
		return fromValueStrict<strictdemo::Record>(document).problems;
	}
} // namespace

TEST_CASE("a document of the right shape reads strictly, numbers in any arm", "[strict]")
{
	const auto read = fromValueStrict<strictdemo::Record>(good());
	REQUIRE(read.value.has_value());
	CHECK(read.problems.empty());
	CHECK(read.value->scale == 0.75f);

	// A count written as a double, or a scale as an integer, is still a number: which numeric arm a
	// codec read it into means nothing.
	CHECK(problemsOf(with("count", Value(3.0))).empty());
	CHECK(problemsOf(with("scale", Value(std::uint64_t{1}))).empty());
}

TEST_CASE("what best-effort reading hides, the strict read names", "[strict]")
{
	// Each of these reads WITHOUT complaint through fromValue, to a default. That is the point of
	// fromValue; it is why these need saying somewhere.
	SECTION("an unknown key")
	{
		const Value document = with("colour", Value("red"));
		CHECK(fromValue<strictdemo::Record>(document).has_value());
		CHECK(problemsOf(document) == std::vector<std::string>{"'colour' is not a known key"});
	}
	SECTION("a missing key")
	{
		CHECK(problemsOf(without("count")) == std::vector<std::string>{"'count' is missing"});
	}
	SECTION("an enum name nothing recognises")
	{
		CHECK(problemsOf(with("shade", Value("Darker"))) == std::vector<std::string>{"'shade' is 'Darker', which was not recognised"});
	}
	SECTION("a variant arm nothing recognises")
	{
		Value shape = Value::object();
		shape.set("type", Value("triangle"));
		shape.set("value", Value::object());
		const std::vector<std::string> problems = problemsOf(with("shape", shape));
		REQUIRE_FALSE(problems.empty());
		CHECK(problems.front() == "'shape.type' is 'triangle', which was not recognised");
	}
	SECTION("a value of the wrong kind")
	{
		CHECK(problemsOf(with("count", Value("3"))) == std::vector<std::string>{"'count' is a string, where a number belongs"});
	}
	SECTION("an array of the wrong length, or holding the wrong kind")
	{
		Value items = Value::array();
		items.asArray()->push_back(Value("x"));
		const std::vector<std::string> problems = problemsOf(with("items", items));
		REQUIRE_FALSE(problems.empty());
	}
	SECTION("an absent optional is not missing")
	{
		CHECK(problemsOf(without("weight")).empty());
	}
}
