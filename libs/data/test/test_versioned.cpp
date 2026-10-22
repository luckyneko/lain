// Versioned documents: the version first, and whether a reader can read one at all.

#include <lain/data/value.h>
#include <lain/data/versioned.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using lain::data::Value;

static Value body()
{
	Value v = Value::object();
	v.set("name", Value(std::string("hd")));
	v.set("width", Value(std::uint64_t{1280}));
	return v;
}

TEST_CASE("a versioned document puts its version first, then the body in order", "[data][versioned]")
{
	const Value document = lain::data::versionedDocument(3, body());
	std::vector<std::string> keys;
	for (const auto& entry : *document.asObject())
		keys.push_back(entry.first);
	REQUIRE(keys == std::vector<std::string>{"version", "name", "width"});
	REQUIRE(document.find("version")->asUInt64() == std::uint64_t{3});
}

TEST_CASE("a document no newer than the reader can be read", "[data][versioned]")
{
	REQUIRE(lain::data::versionProblems(lain::data::versionedDocument(1, body()), "rig", 1).empty());
	REQUIRE(lain::data::versionProblems(lain::data::versionedDocument(1, body()), "rig", 2).empty());
}

TEST_CASE("a document that is not an object, has no version, or a newer one cannot be read", "[data][versioned]")
{
	using Problems = std::vector<std::string>;
	REQUIRE(lain::data::versionProblems(Value(std::uint64_t{1}), "rig", 1) == Problems{"a rig document is an object"});
	REQUIRE(lain::data::versionProblems(body(), "rig", 1) == Problems{"the document has no version"});
	REQUIRE(lain::data::versionProblems(lain::data::versionedDocument(2, body()), "rig", 1) ==
			Problems{"the document is version 2, newer than this build reads (1)"});

	Value textVersion = body();
	textVersion.set("version", Value(std::string("1"))); // a version is a number, not text that looks like one
	REQUIRE(lain::data::versionProblems(textVersion, "rig", 1) == Problems{"the document has no version"});
}
