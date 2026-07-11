// The JSON codec end to end: Value <-> JSON bytes, the reflection two-hop (T -> Value -> JSON ->
// Value -> T), file save/load, idempotent text, and the failure path. Registers the codec directly.

#include "lain/io/data/json/register.h"

#include <lain/data/data.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using lain::data::Value;
using lain::io::data::decode;
using lain::io::data::encode;
using lain::io::data::load;
using lain::io::data::save;

static void ensureJson()
{
	lain::io::data::json::registerCodec(); // registerType replaces — safe to repeat
}

// The bytes of a Buffer as a string, for comparing encoded text.
static std::string text(const lain::memory::Buffer& buffer)
{
	return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

// A Value exercising every unambiguous arm (positive ints as UInt so the DOM arm survives — a
// positive Int would normalise to UInt through JSON; see the codec).
static Value sampleValue()
{
	Value v = Value::object();
	v.set("name", Value("lain"));
	v.set("id", Value(std::uint64_t{42}));	// UInt
	v.set("delta", Value(-7));				// Int (negative stays Int)
	v.set("ratio", Value(0.5));				// Double
	v.set("on", Value(true));
	v.set("nothing", Value());
	Value list = Value::array();
	list.push(Value(std::uint64_t{1})); // UInt: a positive Int would normalise to UInt through JSON
	list.push(Value(std::uint64_t{2}));
	v.set("list", list);
	return v;
}

TEST_CASE("a Value round-trips through JSON bytes", "[json]")
{
	ensureJson();
	const Value v = sampleValue();

	const auto bytes = encode("json", v);
	REQUIRE(bytes.has_value());

	const auto back = decode("json", *bytes);
	REQUIRE(back.has_value());
	REQUIRE(*back == v); // full DOM round-trip, key order preserved
}

TEST_CASE("distinct number arms survive JSON", "[json]")
{
	ensureJson();
	// A negative int stays Int; a float stays Double (no collapse to one JSON number kind).
	REQUIRE(decode("json", *encode("json", Value(-4)))->type() == Value::Type::Int);
	REQUIRE(decode("json", *encode("json", Value(4.0)))->type() == Value::Type::Double);
	// JSON has no signed/unsigned: a positive Int normalises to UInt (documented, harmless).
	REQUIRE(decode("json", *encode("json", Value(4)))->type() == Value::Type::UInt);
}

namespace demo
{
	struct Config
	{
		std::string title;
		int count = 0;
		float gain = 0.0f;
		std::vector<int> steps;

		bool operator==(const Config& o) const
		{
			return title == o.title && count == o.count && gain == o.gain && steps == o.steps;
		}
	};
	LAIN_SERIALIZE(Config, title, count, gain, steps)
} // namespace demo

TEST_CASE("the reflection two-hop: T -> Value -> JSON -> Value -> T", "[json]")
{
	ensureJson();
	demo::Config c{"scene", 3, 0.25f, {5, 6, 7}};

	const auto bytes = encode("json", lain::data::toValue(c));
	REQUIRE(bytes.has_value());

	const auto back = decode("json", *bytes);
	REQUIRE(back.has_value());

	const auto r = lain::data::fromValue<demo::Config>(*back);
	REQUIRE(r.has_value());
	REQUIRE(*r == c); // survives the whole pipeline (positive-int arm flip is cross-accepted)
}

TEST_CASE("text is idempotent: encode . decode . encode == encode", "[json]")
{
	ensureJson();
	const Value v = sampleValue();

	const auto once = encode("json", v);
	REQUIRE(once.has_value());
	const auto twice = encode("json", *decode("json", *once));
	REQUIRE(twice.has_value());
	REQUIRE(text(*once) == text(*twice)); // a file we wrote round-trips byte-identically
}

TEST_CASE("save then load a file round-trips", "[json]")
{
	ensureJson();
	const Value v = sampleValue();
	const auto path = (std::filesystem::temp_directory_path() / "lain_json_roundtrip.json").string();

	REQUIRE(save(path, v));
	const auto loaded = load(path);
	REQUIRE(loaded.has_value());
	REQUIRE(*loaded == v);

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST_CASE("malformed JSON decodes to nullopt", "[json]")
{
	ensureJson();
	const std::string bad = "{ not: valid";
	lain::memory::Buffer buffer(bad.size());
	std::memcpy(buffer.data(), bad.data(), bad.size());

	REQUIRE_FALSE(decode("json", buffer).has_value());
}
