// LAIN_SERIALIZE macros (free + intrusive) and tagged std::variant round-trips.

#include "lain/data/data.h"

#include <catch2/catch_test_macros.hpp>
#include <variant>

using lain::data::fromValue;
using lain::data::toValue;
using lain::data::Value;

namespace demo
{
	// --- the macro: a free, field-named serialize ---
	struct Point
	{
		int x = 0;
		int y = 0;
	};
	LAIN_SERIALIZE(Point, x, y)

	// --- the intrusive macro: private members, member serialize ---
	class Secret
	{
	public:
		Secret() = default;
		Secret(int a, int b)
			: m_a(a)
			, m_b(b)
		{
		}
		int a() const { return m_a; }
		int b() const { return m_b; }

		LAIN_SERIALIZE_INTRUSIVE(m_a, m_b)

	private:
		int m_a = 0;
		int m_b = 0;
	};

	// --- a tagged variant: arms + their stable keys ---
	struct Circle
	{
		double r = 0.0;
	};
	struct Square
	{
		double side = 0.0;
	};
	LAIN_SERIALIZE(Circle, r)
	LAIN_SERIALIZE(Square, side)

	using Shape = std::variant<Circle, Square>;
	LAIN_SERIALIZE_VARIANT_ARM(Circle, "circle")
	LAIN_SERIALIZE_VARIANT_ARM(Square, "square")
} // namespace demo

TEST_CASE("LAIN_SERIALIZE generates a field-named free serialize", "[macro]")
{
	demo::Point p{3, 4};
	Value v = toValue(p);
	REQUIRE(v.find("x")->asInt64() == 3); // field name is the key
	REQUIRE(v.find("y")->asInt64() == 4);

	auto r = fromValue<demo::Point>(v);
	REQUIRE(r.has_value());
	REQUIRE(r->x == 3);
	REQUIRE(r->y == 4);
}

TEST_CASE("LAIN_SERIALIZE_INTRUSIVE reaches private members", "[macro]")
{
	demo::Secret s{7, 9};
	auto r = fromValue<demo::Secret>(toValue(s));
	REQUIRE(r.has_value());
	REQUIRE(r->a() == 7);
	REQUIRE(r->b() == 9);
}

TEST_CASE("a tagged variant round-trips through {type, value}", "[variant]")
{
	demo::Shape s = demo::Circle{2.5};
	Value v = toValue(s);
	REQUIRE(v.find("type")->asString() != nullptr);
	REQUIRE(*v.find("type")->asString() == "circle");		// the stable arm key
	REQUIRE(v.find("value")->find("r")->asDouble() == 2.5); // the arm, serialized nested

	auto r = fromValue<demo::Shape>(v);
	REQUIRE(r.has_value());
	REQUIRE(std::holds_alternative<demo::Circle>(*r));
	REQUIRE(std::get<demo::Circle>(*r).r == 2.5);
}

TEST_CASE("the other arm round-trips; an unknown tag is nullopt", "[variant]")
{
	demo::Shape s = demo::Square{4.0};
	auto r = fromValue<demo::Shape>(toValue(s));
	REQUIRE(r.has_value());
	REQUIRE(std::holds_alternative<demo::Square>(*r));

	// an unknown discriminator conforms to no arm -> nullopt (the type is the schema)
	Value bad = Value::object();
	bad.set("type", Value("triangle"));
	bad.set("value", Value::object());
	REQUIRE_FALSE(fromValue<demo::Shape>(bad).has_value());
}
