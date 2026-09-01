// The sequence manifest — what a sequence-valued output writes instead of pixels.
//
// Two things are worth asserting and they are different: that the document round-trips as data
// (which is what makes it declarative rather than hand-built), and that its WIRE SHAPE is the one
// intended, since a manifest is read by things outside this program.

#include "testsource.h"

#include <lain/data/data.h>
#include <lain/media/serialize/manifest.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace lain;
using namespace lain::media;
using lain::data::Value;

namespace
{
	FrameSequence twoSources()
	{
		auto first = std::make_shared<test::TestSource>("/footage/take1", 3);
		auto second = std::make_shared<test::TestSource>("/footage/take2", 2);

		// Deliberately out of order and across sources, so position and identity disagree — the
		// distinction a manifest exists to record.
		return *FrameSequence::of({{first, 2}, {second, 0}, {first, 0}});
	}
} // namespace

TEST_CASE("a manifest records identity, and lets position stay implicit", "[media::serialize]")
{
	const SequenceManifest manifest = manifestOf(twoSources());

	REQUIRE(manifest.version == 1);
	REQUIRE(manifest.frames.size() == 3);

	// Order is position; the entries are identity. Both are here exactly once.
	CHECK(manifest.frames[0].source == "/footage/take1");
	CHECK(manifest.frames[0].ordinal == 2);
	CHECK(manifest.frames[1].source == "/footage/take2");
	CHECK(manifest.frames[1].ordinal == 0);
	CHECK(manifest.frames[2].ordinal == 0);

	CHECK(manifest.spec == twoSources().spec());
}

TEST_CASE("the manifest round-trips as data", "[media::serialize]")
{
	// It is plain data, so it reflects both ways — which is the whole reason it is declared rather
	// than hand-built. What does NOT round-trip is a FrameSequence, and there is deliberately no
	// serialize for one: Archive is direction-agnostic, so writing one would make
	// fromValue<FrameSequence> compile and silently yield an empty sequence.
	const SequenceManifest manifest = manifestOf(twoSources());

	const Value document = data::toValue(manifest);
	const auto read = data::fromValue<SequenceManifest>(document);
	REQUIRE(read.has_value());

	REQUIRE(read->version == manifest.version);
	REQUIRE(read->frames.size() == manifest.frames.size());
	CHECK(read->frames[0].source == manifest.frames[0].source);
	CHECK(read->frames[0].ordinal == manifest.frames[0].ordinal);
	CHECK(read->spec == manifest.spec);

	// Idempotent: writing what was read gives the same document.
	CHECK(data::toValue(*read) == document);
}

TEST_CASE("the manifest's wire shape is the intended one", "[media::serialize]")
{
	const Value document = data::toValue(manifestOf(twoSources()));

	// Pinned deliberately: a manifest is read by things outside this program, so the key names are
	// a contract and not an implementation detail.
	const Value* spec = document.find("spec");
	REQUIRE(spec != nullptr);
	REQUIRE(spec->find("width") != nullptr);
	REQUIRE(spec->find("height") != nullptr);

	// The extent is flattened to width/height rather than nested, because math::Vec2i is a glm
	// alias that cannot carry a serialize of its own without one that is global to the program.
	CHECK(spec->find("width")->asInt64().value_or(0) == 4);
	CHECK(spec->find("height")->asInt64().value_or(0) == 2);

	// Enums by name, not by integer — what makes the document readable and stable against a
	// reordering of the enum.
	REQUIRE(spec->find("pixelFormat")->asString() != nullptr);
	CHECK(*spec->find("pixelFormat")->asString() == "RGBA8");
	CHECK(*spec->find("colorSpace")->asString() == "sRGB");

	// The rate stays an exact rational: 30000/1001 is a rate and 29.97 is a rounding of one, and a
	// manifest that had been through a double could not configure an encoder without drifting.
	const Value* rate = spec->find("rate");
	REQUIRE(rate != nullptr);
	CHECK(rate->find("numerator")->asUInt64().value_or(0) == 25);
	CHECK(rate->find("denominator")->asUInt64().value_or(0) == 1);

	// No frameCount key: it is frames.size(), and a recorded derivable fact has somewhere to
	// disagree with what it describes.
	CHECK(document.find("frameCount") == nullptr);

	const Value* frames = document.find("frames");
	REQUIRE(frames != nullptr);
	REQUIRE(frames->asArray() != nullptr);
	REQUIRE(frames->asArray()->size() == 3);
	CHECK(*(*frames->asArray())[0].find("source")->asString() == "/footage/take1");
	CHECK((*frames->asArray())[0].find("ordinal")->asUInt64().value_or(99) == 2);
}
