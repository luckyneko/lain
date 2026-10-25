// Rig plans and specification catalogues as documents: what they write, and what a strict read refuses.

#include "testplan.h"

#include <lain/rigplan/realisation.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/testing/rootedpath.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
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

TEST_CASE("a sensor's sensor model reads back as it was written", "[rigplan][serialize]")
{
	// Every field away from its default, so a field both directions dropped could not hide behind the
	// default the read would give it.
	lain::rigplan::RigPlan original = testplan::plan();
	lain::rigplan::SensorModel& model = original.specifications.sensors[0].sensorModel;
	model.gain = 0.75;
	model.fullWellElectrons = 4500;
	model.readNoiseElectrons = 2.25;
	model.adcBits = 10;
	model.channels = lain::rigplan::SensorChannels::Grey;
	model.output.format = lain::rigplan::OutputFormat::Jpeg;
	model.output.curve = lain::image::ColorSpace::BT709;
	model.output.quality = 85;

	const Value document = lain::rigplan::rigPlanToValue(original);
	const lain::rigplan::RigPlanRead read = lain::rigplan::rigPlanFromValue(document);
	for (const std::string& p : read.problems)
		UNSCOPED_INFO(p);
	REQUIRE(read.plan.has_value());
	REQUIRE(lain::rigplan::rigPlanToValue(*read.plan) == document);
	const lain::rigplan::SensorModel& back = read.plan->specifications.sensors[0].sensorModel;
	REQUIRE(back.gain == 0.75);
	REQUIRE(back.fullWellElectrons == 4500);
	REQUIRE(back.readNoiseElectrons == 2.25);
	REQUIRE(back.adcBits == 10);
	REQUIRE(back.channels == lain::rigplan::SensorChannels::Grey);
	REQUIRE(back.output.format == lain::rigplan::OutputFormat::Jpeg);
	REQUIRE(back.output.bits == 8);
	REQUIRE(back.output.curve == lain::image::ColorSpace::BT709);
	REQUIRE(back.output.quality == std::optional<std::uint8_t>(85));
}

TEST_CASE("a rig plan document's shape", "[rigplan][serialize]")
{
	// Pinned, since a document is read by people and by later builds: renaming a key is a migration.
	Value document = lain::rigplan::rigPlanToValue(testplan::plan());
	// A rig plan in no room writes no room; its lists of planes, boxes and props are there, empty.
	REQUIRE(keysOf(document) == std::vector<std::string>{"version", "specifications", "planes", "boxes", "props", "boards", "mounts",
														 "cameras", "lights", "timeline", "seed", "scales"});

	Value& lens = element(member(member(document, "specifications"), "lenses"), 0);
	REQUIRE(keysOf(lens) == std::vector<std::string>{"name", "focalLengthMm", "distortion", "focalLengthTolerance", "distortionTolerance"});
	REQUIRE(member(lens, "focalLengthMm") == Value(4.0));
	REQUIRE(member(member(lens, "distortion"), "type") == Value(std::string("brownConrady5"))); // the camera-model document's key

	Value& sensor = element(member(member(document, "specifications"), "sensors"), 0);
	REQUIRE(member(sensor, "pixelPitchUm") == Value(2.0));
	REQUIRE(keysOf(member(sensor, "sensorModel")) ==
			std::vector<std::string>{"gain", "fullWellElectrons", "readNoiseElectrons", "adcBits", "channels", "output"});
	REQUIRE(member(member(sensor, "sensorModel"), "channels") == Value(std::string("Rgb")));
	Value& output = member(member(sensor, "sensorModel"), "output");
	REQUIRE(member(output, "format") == Value(std::string("Png")));
	REQUIRE(member(output, "curve") == Value(std::string("sRGB")));

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
	SECTION("an output format that does not exist")
	{
		Value& sensor = element(member(member(document, "specifications"), "sensors"), 0);
		member(member(member(sensor, "sensorModel"), "output"), "format") = Value(std::string("Webp"));
		REQUIRE_FALSE(lain::rigplan::rigPlanFromValue(document).plan.has_value());
	}
	SECTION("a misspelled sensor-model key")
	{
		Value& sensor = element(member(member(document, "specifications"), "sensors"), 0);
		rename(member(sensor, "sensorModel"), "fullWellElectrons", "fullWell");
		const auto read = lain::rigplan::rigPlanFromValue(document);
		REQUIRE_FALSE(read.plan.has_value());
		REQUIRE(mentions(read.problems, "fullWell"));
	}
	SECTION("a board dictionary that does not exist")
	{
		lain::rigplan::RigPlan withBoard = testplan::plan();
		withBoard.boards.push_back(testplan::board());
		document = lain::rigplan::rigPlanToValue(withBoard);
		member(member(element(member(document, "boards"), 0), "pattern"), "dictionary") = Value(std::string("Aruco5x5_99"));
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

// --- the environment, and its paths ---------------------------------------------------------

TEST_CASE("a furnished rig plan reads back as it was written", "[rigplan][serialize][environment]")
{
	const lain::rigplan::RigPlan original = testplan::furnished();
	const Value document = lain::rigplan::rigPlanToValue(original);
	const lain::rigplan::RigPlanRead read = lain::rigplan::rigPlanFromValue(document);
	for (const std::string& p : read.problems)
		UNSCOPED_INFO(p);
	REQUIRE(read.plan.has_value());
	REQUIRE(lain::rigplan::rigPlanToValue(*read.plan) == document);

	const lain::rigplan::RigPlan& p = *read.plan;
	REQUIRE(p.room.has_value());
	REQUIRE(p.room->size == original.room->size);
	REQUIRE_FALSE(p.room->ceiling.present);
	REQUIRE(std::get<lain::image::ProceduralTexture>(p.room->floor.material.surface).seed == 11);
	REQUIRE(std::get<lain::rigplan::ImageTexture>(p.room->north.material.surface).image ==
			std::get<lain::rigplan::ImageTexture>(original.room->north.material.surface).image);
	REQUIRE(std::get<lain::rigplan::ImageTexture>(p.room->north.material.surface).tile == lain::math::Vec2d{0.5, 0.25});
	REQUIRE(p.planes[0].aim.panDegrees == 30.0);
	REQUIRE(p.planes[0].material.shininess == 16.0);
	REQUIRE(p.boxes[0].yawDegrees == 15.0);
	REQUIRE(p.props[0].clip);
	REQUIRE(p.props[0].clipRate == lain::media::FrameRate{24, 1});
	REQUIRE(p.props[0].unitMetres == 0.01);
	REQUIRE(p.props[0].up == lain::rigplan::UpAxis::Y);

	// And a rig plan with no room writes none, and reads back with none.
	REQUIRE_FALSE(lain::rigplan::rigPlanToValue(testplan::plan()).find("room"));
}

TEST_CASE("movers read back as they were written", "[rigplan][serialize][mover]")
{
	lain::rigplan::RigPlan original = testplan::furnished();
	original.boards.push_back(testplan::board());
	original.boards[0].margin = 0.015;
	original.boards[0].pixelsPerSquare = 120;
	original.boards[0].backing = 0.005;
	original.boards[0].instance.squareLength.lowerBound = lain::core::Length::from<lain::core::Length::Millimetres>(23.9);
	original.props[0].path = lain::rigplan::Path{{{3, {0.5, -1.0, 0.0}, {10.0, 0.0, 0.0}}, {9, {1.5, -0.5, 0.0}, {40.0, 0.0, 0.0}}}};

	Value document = lain::rigplan::rigPlanToValue(original);
	const lain::rigplan::RigPlanRead read = lain::rigplan::rigPlanFromValue(document);
	for (const std::string& p : read.problems)
		UNSCOPED_INFO(p);
	REQUIRE(read.plan.has_value());
	REQUIRE(lain::rigplan::rigPlanToValue(*read.plan) == document);

	// What the movers do, not only what they write: the same pose at every frame.
	const lain::rigplan::RigPlan& p = *read.plan;
	REQUIRE(p.boards.size() == 1);
	REQUIRE(p.boards[0].pattern.squaresX == 7);
	REQUIRE(p.boards[0].instance.squareLength.value == original.boards[0].instance.squareLength.value);
	REQUIRE(p.boards[0].instance.squareLength.lowerBound == original.boards[0].instance.squareLength.lowerBound);
	REQUIRE(p.boards[0].pixelsPerSquare == 120);
	REQUIRE(p.boards[0].margin == 0.015);
	REQUIRE(p.boards[0].backing == 0.005);
	for (std::size_t f = 0; f < 24; ++f)
	{
		const auto a = lain::rigplan::planFromBoard(original.boards[0], f);
		const auto b = lain::rigplan::planFromBoard(p.boards[0], f);
		REQUIRE(a.rotation() == b.rotation());
		REQUIRE(a.translation() == b.translation());
		REQUIRE(lain::rigplan::planFromProp(original.props[0], f) == lain::rigplan::planFromProp(p.props[0], f));
	}

	// A prop that stands still writes no path; one that moves writes its keys.
	Value& prop = element(member(document, "props"), 0);
	REQUIRE(keysOf(member(member(prop, "path"), "keys").asArray()->front()) == std::vector<std::string>{"frame", "position", "aim"});
	REQUIRE_FALSE(lain::rigplan::rigPlanToValue(testplan::furnished()).find("props")->asArray()->front().find("path"));
	Value& board = element(member(document, "boards"), 0);
	REQUIRE(keysOf(board) ==
			std::vector<std::string>{"id", "name", "pattern", "instance", "margin", "pixelsPerSquare", "backing", "path"});
	REQUIRE(member(member(board, "pattern"), "dictionary") == Value(std::string("Aruco5x5_100"))); // as a board document spells it
}

TEST_CASE("a furnished rig plan document's shape", "[rigplan][serialize][environment]")
{
	Value document = lain::rigplan::rigPlanToValue(testplan::furnished());
	REQUIRE(keysOf(document) == std::vector<std::string>{"version", "specifications", "room", "planes", "boxes", "props",
														 "boards", "mounts", "cameras", "lights", "timeline", "seed", "scales"});
	Value& room = member(document, "room");
	REQUIRE(keysOf(room) == std::vector<std::string>{"centre", "size", "floor", "ceiling", "north", "south", "east", "west"});
	Value& floor = member(member(member(room, "floor"), "material"), "surface");
	REQUIRE(member(floor, "type") == Value(std::string("procedural")));
	REQUIRE(keysOf(member(floor, "value")) == std::vector<std::string>{"seed", "cell"});
	Value& prop = element(member(document, "props"), 0);
	REQUIRE(keysOf(prop) ==
			std::vector<std::string>{"id", "name", "mesh", "clip", "clipRate", "up", "unitMetres", "position", "yawDegrees"});
	REQUIRE(member(prop, "up") == Value(std::string("Y")));
}

TEST_CASE("a rig plan document's paths are relative inside its folder, and absolute outside it", "[rigplan][serialize][environment]")
{
	// The brick texture lies under the document's folder and the walking figure outside it.
	const std::filesystem::path folder = lain::testing::rootedPath("projects/hall");
	Value document = lain::rigplan::rigPlanToValue(testplan::furnished(folder));
	lain::rigplan::relativizePaths(document, folder);

	Value& north = member(member(member(member(document, "room"), "north"), "material"), "surface");
	REQUIRE(member(member(north, "value"), "image") == Value(std::string("textures/brick.png")));
	REQUIRE(member(element(member(document, "props"), 0), "mesh") == Value(lain::testing::rootedPath("assets/people/walk.<frame:04>.obj").generic_string()));

	// Read back from the folder, every path is what the rig plan held.
	lain::rigplan::resolvePaths(document, folder);
	const auto read = lain::rigplan::rigPlanFromValue(document);
	REQUIRE(read.plan.has_value());
	REQUIRE(lain::rigplan::rigPlanToValue(*read.plan) == lain::rigplan::rigPlanToValue(testplan::furnished(folder)));
}

TEST_CASE("relativizing reaches every material a rig plan holds", "[rigplan][serialize][environment]")
{
	// A picture on a plane and on a box too, not only on the room, so each place a material lives is
	// walked.
	const std::filesystem::path folder = lain::testing::rootedPath("projects/hall");
	lain::rigplan::RigPlan plan = testplan::furnished(folder);
	plan.planes[0].material.surface = lain::rigplan::ImageTexture{lain::core::Uri::fromPath(folder / "poster.png"), {1.0, 1.0}};
	plan.boxes[0].material.surface = lain::rigplan::ImageTexture{lain::core::Uri::fromPath(folder / "wood.png"), {1.0, 1.0}};
	Value document = lain::rigplan::rigPlanToValue(plan);
	lain::rigplan::relativizePaths(document, folder);
	const auto pictureOf = [&](Value& item)
	{
		return member(member(member(member(item, "material"), "surface"), "value"), "image");
	};
	REQUIRE(pictureOf(element(member(document, "planes"), 0)) == Value(std::string("poster.png")));
	REQUIRE(pictureOf(element(member(document, "boxes"), 0)) == Value(std::string("wood.png")));
}
