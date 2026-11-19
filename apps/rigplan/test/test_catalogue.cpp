// Copying specifications in from a catalogue and saving them out to one (CONTEXT.md, "Specification
// catalogue"): what a camera specification brings, when two specifications are one, how a clash is
// answered, and what saving out does to the file it is given.

#include "document.h"
#include "edits.h"
#include "selection.h"

#include <lain/core/length.h>
#include <lain/io/data/codecs.h>
#include <lain/io/data/save.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace rp = lain::rigplan;
using lain::core::Length;
using rigplan::ClashAnswer;
using rigplan::SaveOutResult;
using rigplan::SpecificationKind;

static std::string bytesOf(const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void writeBytes(const fs::path& file, const std::string& bytes)
{
	std::ofstream out(file, std::ios::binary);
	out << bytes;
}

static rp::SensorSpecification sensor(const std::string& name, std::uint32_t width, std::uint32_t height, double pitchUm)
{
	rp::SensorSpecification s;
	s.name = name;
	s.image = {width, height};
	s.pixelPitch = Length::from<Length::Micrometres>(pitchUm);
	return s;
}

static rp::LensSpecification lens(const std::string& name, double focalMm)
{
	rp::LensSpecification l;
	l.name = name;
	l.focalLength = Length::from<Length::Millimetres>(focalMm);
	l.distortion = lain::camera::NoDistortion{};
	return l;
}

// The plan's own specifications as a document, to tell a change from none.
static lain::data::Value specificationsOf(const rp::RigPlan& plan)
{
	return rp::catalogueToValue(plan.specifications);
}

// A catalogue holding the new plan's "hd" sensor exactly, a 4K sensor, an 8 mm lens, and two cameras.
static rp::Specifications catalogue()
{
	const rp::RigPlan plan = rigplan::newPlan();
	rp::Specifications c;
	c.sensors = {*rp::findSensor(plan.specifications, "hd"), sensor("uhd", 3840, 2160, 1.4)};
	c.lenses = {lens("8mm", 8.0)};
	c.cameras = {{"uhd-8mm", "uhd", "8mm"}, {"hd-8mm", "hd", "8mm"}};
	return c;
}

TEST_CASE("a camera specification copied in brings its sensor and lens", "[rigplan][catalogue]")
{
	rp::RigPlan plan = rigplan::newPlan();
	const rigplan::EditResult result = rigplan::copyIn(plan, catalogue(), SpecificationKind::Camera, "uhd-8mm");
	REQUIRE(result.changed());
	CHECK(*result.selection == rigplan::Selection::specificationOf(SpecificationKind::Camera, "uhd-8mm"));
	REQUIRE(rp::findCamera(plan.specifications, "uhd-8mm"));
	REQUIRE(rp::findSensor(plan.specifications, "uhd"));
	REQUIRE(rp::findLens(plan.specifications, "8mm"));
	CHECK(rp::check(plan.specifications).empty());
	const lain::camera::ModelResult model = rp::nominalModel(*rp::findSensor(plan.specifications, "uhd"), *rp::findLens(plan.specifications, "8mm"));
	REQUIRE(model.model);
	CHECK(model.model->image().width == 3840);
}

TEST_CASE("a specification the plan has identically is the plan's", "[rigplan][catalogue]")
{
	rp::RigPlan plan = rigplan::newPlan();
	REQUIRE(rigplan::copyIn(plan, catalogue(), SpecificationKind::Camera, "hd-8mm").changed());
	REQUIRE(plan.specifications.sensors.size() == 1); // no "hd 2": the catalogue's "hd" is the plan's
	CHECK(plan.specifications.sensors[0].name == "hd");
	CHECK(rp::findCamera(plan.specifications, "hd-8mm")->sensor == "hd");

	// Everything it would bring is there already: nothing to do.
	const lain::data::Value before = specificationsOf(plan);
	CHECK_FALSE(rigplan::copyIn(plan, catalogue(), SpecificationKind::Sensor, "hd").changed());
	CHECK_FALSE(rigplan::copyIn(plan, catalogue(), SpecificationKind::Camera, "hd-8mm").changed());
	CHECK(specificationsOf(plan) == before);
}

TEST_CASE("a different specification of a name the plan uses arrives numbered, its references following", "[rigplan][catalogue]")
{
	// A catalogue whose "hd" is a 1080p sensor, its "4mm" the plan's exactly, and an "hd-4mm" of the two.
	const rp::RigPlan fresh = rigplan::newPlan();
	rp::Specifications other;
	other.sensors = {sensor("hd", 1920, 1080, 3.0)};
	other.lenses = {*rp::findLens(fresh.specifications, "4mm")};
	other.cameras = {{"hd-4mm", "hd", "4mm"}};

	rp::RigPlan plan = rigplan::newPlan();
	const rigplan::EditResult result = rigplan::copyIn(plan, other, SpecificationKind::Camera, "hd-4mm");
	REQUIRE(result.changed());
	CHECK(*result.selection == rigplan::Selection::specificationOf(SpecificationKind::Camera, "hd-4mm 2"));
	CHECK(plan.specifications.lenses.size() == 1); // the plan's "4mm" was the catalogue's

	// The plan's own are as they were, and what came in names what came in with it.
	CHECK(rp::findSensor(plan.specifications, "hd")->image.width == 1280);
	CHECK(rp::findCamera(plan.specifications, "hd-4mm")->sensor == "hd");
	const rp::CameraSpecification* arrived = rp::findCamera(plan.specifications, "hd-4mm 2");
	REQUIRE(arrived);
	CHECK(arrived->sensor == "hd 2");
	CHECK(arrived->lens == "4mm");
	REQUIRE(rp::findSensor(plan.specifications, "hd 2"));
	CHECK(rp::findSensor(plan.specifications, "hd 2")->image.width == 1920);
	CHECK(rp::check(plan.specifications).empty());

	// Names compare exactly, as check() compares them: "HD" is not "hd".
	rp::Specifications upper;
	upper.sensors = {sensor("HD", 640, 480, 5.0)};
	REQUIRE(rigplan::copyIn(plan, upper, SpecificationKind::Sensor, "HD").changed());
	CHECK(rp::findSensor(plan.specifications, "HD"));
}

TEST_CASE("a camera specification without its sensor or lens beside it is refused, changing nothing", "[rigplan][catalogue]")
{
	rp::Specifications broken = catalogue();
	broken.cameras.push_back({"ghost", "nosuch", "8mm"});
	rp::RigPlan plan = rigplan::newPlan();
	const lain::data::Value before = specificationsOf(plan);

	const rigplan::EditResult refused = rigplan::copyIn(plan, broken, SpecificationKind::Camera, "ghost");
	CHECK_FALSE(refused.changed());
	CHECK(refused.refusal.find("nosuch") != std::string::npos);
	CHECK(specificationsOf(plan) == before);

	CHECK_FALSE(rigplan::copyIn(plan, broken, SpecificationKind::Lens, "50mm").refusal.empty());
	CHECK(specificationsOf(plan) == before);
}

TEST_CASE("copying a specification in is one step of history", "[rigplan][catalogue]")
{
	rigplan::Document document = rigplan::newDocument();
	const lain::data::Value before = rigplan::snapshot(document.plan);
	REQUIRE(rigplan::copyIn(document.plan, catalogue(), SpecificationKind::Camera, "uhd-8mm").changed());
	document.markChanged();
	rigplan::commit(document);

	REQUIRE(rigplan::undo(document));
	CHECK(rigplan::snapshot(document.plan) == before);
	CHECK_FALSE(rigplan::undo(document));
	REQUIRE(rigplan::redo(document));
	CHECK(rp::findCamera(document.plan.specifications, "uhd-8mm"));
}

TEST_CASE("saving out into a file that is not a catalogue is refused, and leaves it byte for byte", "[rigplan][catalogue]")
{
	lain::io::data::registerDataCodecs();
	const fs::path folder = lain::testing::scratchDir() / "not-catalogues";
	fs::create_directories(folder);
	const rp::RigPlan plan = rigplan::newPlan();

	// Not a document at all, and a document of another kind: a rig plan.
	writeBytes(folder / "garbage.json", "{ this is not json");
	REQUIRE(lain::io::data::save(lain::core::Uri::fromPath(folder / "plan.json"), rp::rigPlanToValue(plan)));
	for (const char* name : {"garbage.json", "plan.json"})
	{
		INFO(name);
		const std::string bytes = bytesOf(folder / name);
		const SaveOutResult result = rigplan::saveCatalogue(folder / name, plan.specifications, SpecificationKind::Camera, "hd-4mm", std::nullopt);
		CHECK(result.outcome == SaveOutResult::Outcome::Refused);
		CHECK(result.refusal.find(name) != std::string::npos);
		CHECK(bytesOf(folder / name) == bytes);
		// An answer changes nothing: there is no catalogue to answer a clash in.
		CHECK(rigplan::saveCatalogue(folder / name, plan.specifications, SpecificationKind::Camera, "hd-4mm", ClashAnswer::Replace).outcome ==
			  SaveOutResult::Outcome::Refused);
		CHECK(bytesOf(folder / name) == bytes);
	}
}

TEST_CASE("a specification saved out to a new catalogue and copied back in is the same", "[rigplan][catalogue]")
{
	lain::io::data::registerDataCodecs();
	const fs::path file = lain::testing::scratchDir() / "new-catalogue.json";
	fs::remove(file);
	const rp::RigPlan plan = rigplan::newPlan();

	const SaveOutResult saved = rigplan::saveCatalogue(file, plan.specifications, SpecificationKind::Camera, "hd-4mm", std::nullopt);
	REQUIRE(saved.outcome == SaveOutResult::Outcome::Saved);
	CHECK(saved.savedAs == "hd-4mm");
	const rigplan::CatalogueFileRead read = rigplan::readCatalogue(file);
	REQUIRE(read.specifications);
	CHECK(read.specifications->sensors.size() == 1);
	CHECK(read.specifications->lenses.size() == 1);
	CHECK(read.specifications->cameras.size() == 1);
	CHECK(read.specifications->lights.empty()); // only what the camera specification brings

	// Into a plan with none, it brings back what was saved, exactly.
	rp::RigPlan empty;
	REQUIRE(rigplan::copyIn(empty, *read.specifications, SpecificationKind::Camera, "hd-4mm").changed());
	rp::Specifications expected = plan.specifications;
	expected.lights.clear();
	CHECK(rp::catalogueToValue(empty.specifications) == rp::catalogueToValue(expected));
}

TEST_CASE("saving out over a different specification of its name asks, and each answer does what it says", "[rigplan][catalogue]")
{
	lain::io::data::registerDataCodecs();
	const fs::path file = lain::testing::scratchDir() / "clashing-catalogue.json";
	const rp::RigPlan plan = rigplan::newPlan();
	// The catalogue's "hd" is a 1080p sensor; its "4mm" is the plan's exactly.
	rp::Specifications held;
	held.sensors = {sensor("hd", 1920, 1080, 3.0)};
	held.lenses = {*rp::findLens(plan.specifications, "4mm")};
	auto reset = [&]
	{ REQUIRE(lain::io::data::save(lain::core::Uri::fromPath(file), rp::catalogueToValue(held))); };
	auto save = [&](std::optional<ClashAnswer> answer)
	{ return rigplan::saveCatalogue(file, plan.specifications, SpecificationKind::Camera, "hd-4mm", answer); };

	reset();
	const std::string bytes = bytesOf(file);
	const SaveOutResult asked = save(std::nullopt);
	REQUIRE(asked.outcome == SaveOutResult::Outcome::Clash);
	CHECK(asked.clashes == std::vector<std::string>{"sensor 'hd'"});
	CHECK(bytesOf(file) == bytes);
	CHECK(save(ClashAnswer::Cancel).outcome == SaveOutResult::Outcome::Cancelled);
	CHECK(bytesOf(file) == bytes);

	// Replace: the catalogue's "hd" is the plan's now.
	REQUIRE(save(ClashAnswer::Replace).outcome == SaveOutResult::Outcome::Saved);
	{
		const rigplan::CatalogueFileRead read = rigplan::readCatalogue(file);
		REQUIRE(read.specifications);
		REQUIRE(read.specifications->sensors.size() == 1);
		CHECK(read.specifications->sensors[0].image.width == 1280);
		CHECK(rp::findCamera(*read.specifications, "hd-4mm")->sensor == "hd");
		CHECK(rp::check(*read.specifications).empty());
	}

	// Keep both: the plan's "hd" is saved as "hd 2", and the camera specification names it.
	reset();
	const SaveOutResult kept = save(ClashAnswer::KeepBoth);
	REQUIRE(kept.outcome == SaveOutResult::Outcome::Saved);
	CHECK(kept.savedAs == "hd-4mm");
	const rigplan::CatalogueFileRead read = rigplan::readCatalogue(file);
	REQUIRE(read.specifications);
	CHECK(rp::findSensor(*read.specifications, "hd")->image.width == 1920);
	REQUIRE(rp::findSensor(*read.specifications, "hd 2"));
	CHECK(rp::findSensor(*read.specifications, "hd 2")->image.width == 1280);
	CHECK(rp::findCamera(*read.specifications, "hd-4mm")->sensor == "hd 2");
	CHECK(read.specifications->lenses.size() == 1);
	CHECK(rp::check(*read.specifications).empty());
}
