// Unit tests for the Value DOM itself — arms, extraction, ordered objects, equality.

#include "lain/data/value.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using lain::data::Value;

TEST_CASE("scalars carry their distinct arm", "[value]")
{
	REQUIRE(Value().type() == Value::Type::Null);
	REQUIRE(Value(nullptr).type() == Value::Type::Null);
	REQUIRE(Value(true).type() == Value::Type::Bool);
	REQUIRE(Value(42).type() == Value::Type::Int);					   // signed int -> Int
	REQUIRE(Value(std::uint64_t{42}).type() == Value::Type::UInt);	   // unsigned -> UInt
	REQUIRE(Value(3.5).type() == Value::Type::Double);
	REQUIRE(Value("hi").type() == Value::Type::String);

	// int and double are DISTINCT arms — the property that keeps NodeIds exact and files stable.
	REQUIRE(Value(4) != Value(4.0));
}

TEST_CASE("scalar extraction is pure and typed", "[value]")
{
	REQUIRE(Value(true).asBool() == true);
	REQUIRE_FALSE(Value(true).asInt64().has_value()); // mismatch -> nullopt, no coercion

	REQUIRE(Value(std::int64_t{-7}).asInt64() == -7);
	REQUIRE(Value(std::uint64_t{7}).asUInt64() == 7u);

	// asDouble widens an integer arm; asInt64/asUInt64 stay exact-arm only.
	REQUIRE(Value(5).asDouble() == 5.0);
	REQUIRE(Value(2.5).asDouble() == 2.5);
	REQUIRE_FALSE(Value(2.5).asInt64().has_value());

	const std::string* s = Value("text").asString();
	REQUIRE(s != nullptr);
	REQUIRE(*s == "text");
}

TEST_CASE("objects are insertion-ordered with upsert", "[value]")
{
	Value o = Value::object();
	o.set("b", Value(1));
	o.set("a", Value(2));
	o.set("b", Value(3)); // upsert in place, no reorder

	const Value::Object* obj = o.asObject();
	REQUIRE(obj != nullptr);
	REQUIRE(obj->size() == 2);
	REQUIRE((*obj)[0].first == "b"); // still first
	REQUIRE((*obj)[1].first == "a");

	const Value* b = o.find("b");
	REQUIRE(b != nullptr);
	REQUIRE(b->asInt64() == 3);
	REQUIRE(o.find("missing") == nullptr);
}

TEST_CASE("arrays append and compare by value", "[value]")
{
	Value a = Value::array();
	a.push(Value(1));
	a.push(Value(2));

	const Value::Array* arr = a.asArray();
	REQUIRE(arr != nullptr);
	REQUIRE(arr->size() == 2);

	Value b = Value::array();
	b.push(Value(1));
	b.push(Value(2));
	REQUIRE(a == b);

	b.push(Value(3));
	REQUIRE(a != b);
}

TEST_CASE("find on a non-object is null, not a crash", "[value]")
{
	REQUIRE(Value(1).find("x") == nullptr);
	REQUIRE(Value("s").asObject() == nullptr);
}
