// Rig plans and specification catalogues as documents: what they write, and what a strict read refuses.

#include "testplan.h"

#include <lain/rigplan/realisation.h>
#include <lain/rigplan/serialize/rigplan.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using lain::data::Value;

// --- editing a document, as a person might ---------------------------------------------------

static Value& member(Value& object, std::string_view key)
{
	for (auto& [k, v] : *object.asObject())
	{
		if (k == key)
			return v;
	}
	FAIL("no member " << key);
	throw; // unreachable: FAIL throws
}

static Value& element(Value& array, std::size_t index)
{
	return (*array.asArray())[index];
}

static void rename(Value& object, std::string_view from, std::string to)
{
	for (auto& entry : *object.asObject())
	{
		if (entry.first == from)
			entry.first = std::move(to);
	}
}

static void remove(Value& object, std::string_view key)
{
	auto& entries = *object.asObject();
	entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& e)
								 { return e.first == key; }),
				  entries.end());
}

static std::vector<std::string> keysOf(const Value& object)
{
	std::vector<std::string> keys;
	for (const auto& entry : *object.asObject())
		keys.push_back(entry.first);
	return keys;
}

static bool mentions(const std::vector<std::string>& problems, std::string_view text)
{
	return std::any_of(problems.begin(), problems.end(), [&](const std::string& p)
					   { return p.find(text) != std::string::npos; });
}

// --- the round trip ------------------------------------------------------------------------

TEST_CASE("a rig plan reads back as it was written, and writes back the same", "[rigplan][serialize]")
{
	const lain::rigplan::RigPlan original = testplan::plan();
	const Value document = lain::rigplan::rigPlanToValue(original);
	const lain::rigplan::RigPlanRead read = lain::rigplan::rigPlanFromValue(document);
	for (const std::string& p : read.problems)
		UNSCOPED_INFO(p);
	REQUIRE(read.plan.has_value());
	REQUIRE(lain::rigplan::rigPlanToValue(*read.plan) == document);

	// Writing back the same could hide a field both directions drop, so what the read plan BUILDS is
	// compared too: every draw depends on the seed, the identities and the tolerances, and every pose on
	// the offsets, aims and mounts.
	const auto a = lain::rigplan::realise(original);
	const auto b = lain::rigplan::realise(*read.plan);
	REQUIRE(a.realisation.has_value());
	REQUIRE(b.realisation.has_value());
	for (std::size_t i = 0; i < a.realisation->cameras.size(); ++i)
	{
		const auto& x = a.realisation->cameras[i];
		const auto& y = b.realisation->cameras[i];
		REQUIRE(x.id == y.id);
		REQUIRE(x.model.parameters().intrinsics.fx == y.model.parameters().intrinsics.fx);
		REQUIRE(x.model.parameters().intrinsics.cx == y.model.parameters().intrinsics.cx);
		REQUIRE(x.planFromCamera.rotation() == y.planFromCamera.rotation());
		REQUIRE(x.planFromCamera.translation() == y.planFromCamera.translation());
	}
	REQUIRE(a.realisation->lights[0].planFromLight.translation() == b.realisation->lights[0].planFromLight.translation());

	// And what no draw reads.
	const lain::rigplan::RigPlan& p = *read.plan;
	REQUIRE(p.mounts[0].name == "north pole");
	REQUIRE(p.cameras[1].name == "cam1");
	REQUIRE(p.specifications.sensors[0].body.size == original.specifications.sensors[0].body.size);
	REQUIRE(p.specifications.lights[0].kind == lain::rigplan::LightKind::Spot);
	REQUIRE(p.specifications.lights[0].colour == original.specifications.lights[0].colour);
	REQUIRE(p.timeline.frames == 24);
	REQUIRE(p.scales.locked == original.scales.locked);
	REQUIRE(p.scales.noise == original.scales.noise);
}

TEST_CASE("a rig plan document's shape", "[rigplan][serialize]")
{
	// Pinned, since a document is read by people and by later builds: renaming a key is a migration.
	Value document = lain::rigplan::rigPlanToValue(testplan::plan());
	REQUIRE(keysOf(document) ==
			std::vector<std::string>{"version", "specifications", "mounts", "cameras", "lights", "timeline", "seed", "scales"});

	Value& lens = element(member(member(document, "specifications"), "lenses"), 0);
	REQUIRE(keysOf(lens) == std::vector<std::string>{"name", "focalLengthMm", "distortion", "focalLengthTolerance", "distortionTolerance"});
	REQUIRE(member(lens, "focalLengthMm") == Value(4.0));
	REQUIRE(member(member(lens, "distortion"), "type") == Value(std::string("brownConrady5"))); // the camera-model document's key

	Value& sensor = element(member(member(document, "specifications"), "sensors"), 0);
	REQUIRE(member(sensor, "pixelPitchUm") == Value(2.0));

	Value& pole = element(member(document, "mounts"), 0);
	REQUIRE(member(pole, "id") == Value(testplan::poleId.toString()));
	REQUIRE(member(member(pole, "kind"), "type") == Value(std::string("pole")));
	REQUIRE(member(member(pole, "tolerance"), "positionMm") == Value(10.0));

	Value& cam1 = element(member(document, "cameras"), 1);
	REQUIRE(keysOf(member(cam1, "aim")) == std::vector<std::string>{"panDegrees", "tiltDegrees", "rollDegrees"});
	REQUIRE(member(member(cam1, "aim"), "tiltDegrees") == Value(-15.0));
}

// --- a strict read -------------------------------------------------------------------------

TEST_CASE("a strict read names what a hand edit got wrong", "[rigplan][serialize]")
{
	const Value written = lain::rigplan::rigPlanToValue(testplan::plan());
	Value document = written;

	SECTION("a misspelled key")
	{
		rename(element(member(member(document, "specifications"), "lenses"), 0), "focalLengthMm", "focalLenghtMm");
		const auto read = lain::rigplan::rigPlanFromValue(document);
		REQUIRE_FALSE(read.plan.has_value());
		REQUIRE(mentions(read.problems, "focalLenghtMm"));
	}
	SECTION("a missing key")
	{
		remove(document, "seed");
		const auto read = lain::rigplan::rigPlanFromValue(document);
		REQUIRE_FALSE(read.plan.has_value());
		REQUIRE(mentions(read.problems, "seed"));
	}
	SECTION("a value of the wrong kind")
	{
		member(document, "seed") = Value(std::string("seven"));
		REQUIRE_FALSE(lain::rigplan::rigPlanFromValue(document).plan.has_value());
	}
	SECTION("a light kind that does not exist")
	{
		member(element(member(member(document, "specifications"), "lights"), 0), "kind") = Value(std::string("Floodlight"));
		REQUIRE_FALSE(lain::rigplan::rigPlanFromValue(document).plan.has_value());
	}
	SECTION("a distortion model that does not exist")
	{
		Value& distortion = member(element(member(member(document, "specifications"), "lenses"), 0), "distortion");
		member(distortion, "type") = Value(std::string("brownConrady6"));
		REQUIRE_FALSE(lain::rigplan::rigPlanFromValue(document).plan.has_value());
	}
	SECTION("an identity that is not a uuid")
	{
		member(element(member(document, "cameras"), 0), "mount") = Value(std::string("north pole"));
		const auto read = lain::rigplan::rigPlanFromValue(document);
		REQUIRE_FALSE(read.plan.has_value());
		REQUIRE(mentions(read.problems, "cameras[0].mount"));
	}
	SECTION("a length no length can be")
	{
		// core::Length would assert on it; the reader refuses it first.
		member(element(member(member(document, "specifications"), "lenses"), 0), "focalLengthMm") = Value(1e300);
		const auto read = lain::rigplan::rigPlanFromValue(document);
		REQUIRE_FALSE(read.plan.has_value());
		REQUIRE(mentions(read.problems, "focalLengthMm"));
	}
	SECTION("a newer version")
	{
		member(document, "version") = Value(std::uint64_t{2});
		const auto read = lain::rigplan::rigPlanFromValue(document);
		REQUIRE_FALSE(read.plan.has_value());
		REQUIRE(mentions(read.problems, "newer than this build reads"));
	}
}

TEST_CASE("reading judges a rig plan's shape, not its content", "[rigplan][serialize]")
{
	// A rig plan with a mistake in it must still open, so it can be fixed; check() is what refuses it.
	lain::rigplan::RigPlan plan = testplan::plan();
	plan.cameras[0].camera = "hd-35mm";
	const auto read = lain::rigplan::rigPlanFromValue(lain::rigplan::rigPlanToValue(plan));
	REQUIRE(read.plan.has_value());
	const auto problems = lain::rigplan::check(*read.plan);
	REQUIRE(problems.size() == 1);
	REQUIRE(problems[0].problem == lain::rigplan::PlanProblem::UnknownSpecification);
}

// --- the catalogue -------------------------------------------------------------------------

TEST_CASE("a specification catalogue is a document of specifications", "[rigplan][serialize]")
{
	const lain::rigplan::Specifications specifications = testplan::plan().specifications;
	const Value document = lain::rigplan::catalogueToValue(specifications);
	REQUIRE(keysOf(document) == std::vector<std::string>{"version", "sensors", "lenses", "cameras", "lights"});

	const lain::rigplan::CatalogueRead read = lain::rigplan::catalogueFromValue(document);
	REQUIRE(read.specifications.has_value());
	REQUIRE(lain::rigplan::catalogueToValue(*read.specifications) == document);
	REQUIRE(read.specifications->lenses[0].focalLength == specifications.lenses[0].focalLength);

	// A rig plan is not a catalogue, though it holds one.
	const auto wrong = lain::rigplan::catalogueFromValue(lain::rigplan::rigPlanToValue(testplan::plan()));
	REQUIRE_FALSE(wrong.specifications.has_value());
	REQUIRE(lain::rigplan::catalogueFromValue(Value(std::uint64_t{1})).problems ==
			std::vector<std::string>{"a specification-catalogue document is an object"});
}
