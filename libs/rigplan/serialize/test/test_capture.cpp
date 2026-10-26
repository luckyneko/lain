// A synthetic capture's manifest as a document: capture.json read back as it was written, its rig plan
// embedded whole with its paths relative to the capture folder, and a hand edit refused.

#include "testplan.h"

#include <lain/rigplan/serialize/capture.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/testing/rootedpath.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using lain::data::Value;
using namespace lain::rigplan;

static Value* member(Value& object, std::string_view key)
{
	for (auto& [k, v] : *object.asObject())
	{
		if (k == key)
			return &v;
	}
	return nullptr;
}

static std::vector<std::string> keysOf(const Value& object)
{
	std::vector<std::string> keys;
	for (const auto& entry : *object.asObject())
		keys.push_back(entry.first);
	return keys;
}

static CaptureManifest manifest()
{
	CaptureManifest m;
	m.generator = "rigplan 0.1.0";
	m.device = "Apple M4 Pro";
	m.deviceKind = "hardware";
	m.plan = testplan::furnished(lain::testing::rootedPath("captures/hall"));
	m.plan.seed = 42;
	for (const PlacedCamera& camera : m.plan.cameras)
	{
		CaptureCamera c;
		c.id = camera.id;
		c.name = camera.name;
		c.output = {OutputFormat::Jpeg, 8, lain::image::ColorSpace::sRGB, 90};
		c.record.device = {"synthetic", camera.camera, camera.id.toString()};
		c.record.stream = {1280, 720, "RGB8", 24.0};
		c.record.properties = {{"sensor", "hd"}};
		m.cameras.push_back(c);
	}
	return m;
}

TEST_CASE("a capture manifest reads back as it was written", "[rigplan][serialize][capture]")
{
	const std::filesystem::path folder = lain::testing::rootedPath("captures/hall");
	const CaptureManifest written = manifest();
	const Value document = captureManifestToValue(written, folder);
	CHECK(keysOf(document) == std::vector<std::string>{"version", "generator", "device", "shutter", "rigPlan", "cameras"});
	Value edited = document;
	CHECK(*member((*member(edited, "cameras")->asArray())[0], "output")->find("lossy")->asBool() == true); // JPEG

	const CaptureManifestRead read = captureManifestFromValue(document, folder);
	INFO((read.problems.empty() ? "" : read.problems[0]));
	REQUIRE(read.manifest.has_value());
	CHECK(read.manifest->generator == "rigplan 0.1.0");
	CHECK(read.manifest->device == "Apple M4 Pro");
	CHECK(read.manifest->deviceKind == "hardware");
	CHECK(read.manifest->shutter == Shutter::Global);
	CHECK(read.manifest->plan.seed == 42);
	// The plan comes back whole: written again, it is the same document.
	CHECK(rigPlanToValue(read.manifest->plan) == rigPlanToValue(written.plan));
	REQUIRE(read.manifest->cameras.size() == written.cameras.size());
	CHECK(read.manifest->cameras[1].id == written.cameras[1].id);
	CHECK(read.manifest->cameras[1].output.quality == std::optional<std::uint8_t>{90});
	CHECK(read.manifest->cameras[1].record.stream.framesPerSecond == std::optional<double>{24.0});
}

TEST_CASE("a capture manifest's rig plan names its pictures relative to the capture folder", "[rigplan][serialize][capture]")
{
	// The furnished plan's brick picture sits inside the folder, its walker's mesh outside it (ADR-0027).
	const std::filesystem::path folder = lain::testing::rootedPath("captures/hall");
	Value document = captureManifestToValue(manifest(), folder);
	Value& plan = *member(document, "rigPlan");
	const Value* picture = member(*member(*member(*member(*member(plan, "room"), "north"), "material"), "surface"), "value")->find("image");
	CHECK(*picture->asString() == "textures/brick.png");
	const Value* mesh = (*member(plan, "props")->asArray())[0].find("mesh");
	CHECK(*mesh->asString() == lain::testing::rootedPath("assets/people/walk.<frame:04>.obj").generic_string());
}

TEST_CASE("a capture manifest a hand edit got wrong is refused, naming the edit", "[rigplan][serialize][capture]")
{
	const std::filesystem::path folder = lain::testing::rootedPath("captures/hall");
	SECTION("its rig plan missing")
	{
		Value document = captureManifestToValue(manifest(), folder);
		auto& entries = *document.asObject();
		entries.erase(std::find_if(entries.begin(), entries.end(), [](const auto& e)
								   { return e.first == "rigPlan"; }));
		const CaptureManifestRead read = captureManifestFromValue(document, folder);
		CHECK_FALSE(read.manifest.has_value());
		REQUIRE(read.problems.size() == 1);
		CHECK(read.problems[0].find("'rigPlan' is missing") != std::string::npos);
	}
	SECTION("a JPEG camera said to be lossless")
	{
		Value document = captureManifestToValue(manifest(), folder);
		Value& output = *member((*member(document, "cameras")->asArray())[0], "output");
		*member(output, "lossy") = Value(false);
		const CaptureManifestRead read = captureManifestFromValue(document, folder);
		CHECK_FALSE(read.manifest.has_value());
		REQUIRE_FALSE(read.problems.empty());
		CHECK(read.problems[0].find("'cameras[0].output.lossy'") != std::string::npos);
	}
	SECTION("a misspelled key")
	{
		Value document = captureManifestToValue(manifest(), folder);
		Value* generator = member(document, "generator");
		Value moved = *generator;
		auto& entries = *document.asObject();
		entries.erase(std::find_if(entries.begin(), entries.end(), [](const auto& e)
								   { return e.first == "generator"; }));
		document.set("generater", moved);
		const CaptureManifestRead read = captureManifestFromValue(document, folder);
		CHECK_FALSE(read.manifest.has_value());
		CHECK_FALSE(read.problems.empty());
	}
	SECTION("a problem in its rig plan, named under it")
	{
		Value document = captureManifestToValue(manifest(), folder);
		*member(*member(document, "rigPlan"), "seed") = Value("forty-two");
		const CaptureManifestRead read = captureManifestFromValue(document, folder);
		CHECK_FALSE(read.manifest.has_value());
		REQUIRE_FALSE(read.problems.empty());
		CHECK(read.problems[0].rfind("'rigPlan': ", 0) == 0);
	}
}
