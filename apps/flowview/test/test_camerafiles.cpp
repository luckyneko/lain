// Camera models and a rig's inputs as files — the one reading the cli's binders and the gui's editors
// both call, and the one writing `run --model` and the gui's Save... both call.
//
// Compiles the production camerafiles.cpp and clibinders.cpp, so each case also holds the cli's binder
// to the routine's answer: the gui's editor calls that routine and nothing else, and has no ImGui-free
// half to call here, so a binder that agrees with the routine is what keeps the two media agreeing.

#include "camerafiles.h"
#include "clibinders.h"
#include "graphio.h"

#include <lain/camera/cameramodel.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/core/uri.h>
#include <lain/flow/portvalue.h>
#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/openers.h>
#include <lain/media/framesequence.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>

using namespace lain;
namespace fs = std::filesystem;

// --- file-local helpers (named static, not an anonymous namespace) ----------

static void ensureRegistered()
{
	static const bool once = []
	{
		io::image::registerImageCodecs();
		io::sequence::registerSequenceOpeners();
		flowview::registerSceneSerialization(); // the json codec
		return true;
	}();
	(void)once;
}

static flowview::BoundaryBinders builtIn()
{
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	return binders;
}

// A fresh, empty folder of the scratch directory.
static fs::path freshDir(const std::string& name)
{
	const fs::path dir = lain::testing::scratchDir() / name;
	fs::remove_all(dir);
	fs::create_directories(dir);
	return dir;
}

// A pinhole told apart from its neighbours by its horizontal focal length.
static camera::CameraModel pinhole(double fx)
{
	camera::CameraModelParameters p;
	p.image = {640, 480};
	p.intrinsics = {fx, 501.5, 319.75, 240.125};
	p.distortion = camera::BrownConrady5{-0.12, 0.05, 0.0004, -0.0003, -0.01};
	return *camera::CameraModel::create(p).model;
}

static void writeText(const fs::path& file, const std::string& text)
{
	std::ofstream(file, std::ios::binary) << text;
}

static std::string readText(const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A copy of a model document with one coefficient's name typed wrong.
static void writeMisspelled(const fs::path& from, const fs::path& to)
{
	std::string text = readText(from);
	const std::size_t at = text.find("\"k2\"");
	REQUIRE(at != std::string::npos);
	text.replace(at, 4, "\"kk2\"");
	writeText(to, text);
}

// Whether any problem mentions every one of `parts`.
static bool named(const std::vector<std::string>& problems, const std::vector<std::string>& parts)
{
	return std::any_of(problems.begin(), problems.end(), [&](const std::string& problem)
					   { return std::all_of(parts.begin(), parts.end(), [&](const std::string& part)
											{ return problem.find(part) != std::string::npos; }); });
}

// One camera's footage: a folder of two stills `width` pixels wide.
static void writeStills(const fs::path& folder, int width)
{
	fs::create_directories(folder);
	for (const char* name : {"frame.0000.png", "frame.0001.png"})
		REQUIRE(io::image::save(core::Uri::fromPath(folder / name), image::Image(width, 8)));
}

// --- cases ------------------------------------------------------------------

TEST_CASE("a camera model saved is the model loaded, and the one the cli binds", "[flowview][camerafiles]")
{
	// What the gui's Save... and `run --model` write, read back as the gui's editor and `--imported` read
	// it. A typo is refused with the file and the key named, since that is all a person has to go on.
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-model");
	const camera::CameraModel model = pinhole(512.25);
	const fs::path file = dir / "model.json";
	REQUIRE(flowview::saveCameraModel(core::Uri::fromPath(file), model));

	const flowview::FileResult<camera::CameraModel> loaded = flowview::loadCameraModel(core::Uri::fromPath(file));
	REQUIRE(loaded.value.has_value());
	CHECK(loaded.problems.empty());
	CHECK(camera::cameraModelToValue(*loaded.value) == camera::cameraModelToValue(model));

	const std::optional<flow::PortValue> bound = builtIn().bind(typeid(camera::CameraModel), file.string());
	REQUIRE(bound.has_value());
	CHECK(camera::cameraModelToValue(bound->get<camera::CameraModel>()) == camera::cameraModelToValue(model));

	const fs::path typo = dir / "typo.json";
	writeMisspelled(file, typo);
	const flowview::FileResult<camera::CameraModel> refused = flowview::loadCameraModel(core::Uri::fromPath(typo));
	CHECK_FALSE(refused.value.has_value());
	CHECK(named(refused.problems, {typo.string(), "kk2"}));
	CHECK_FALSE(builtIn().bind(typeid(camera::CameraModel), typo.string()).has_value());
}

TEST_CASE("a rig's models load by name, skipping hidden entries and other files", "[flowview][camerafiles]")
{
	// Six cameras written out of name order, so a listing in the file system's own order would show. A
	// hidden document that is not one at all, and a note, sit beside them: reading either refuses the rig.
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-rig-models");
	for (const int camera : {3, 0, 5, 1, 4, 2})
	{
		REQUIRE(flowview::saveCameraModel(core::Uri::fromPath(dir / ("cam" + std::to_string(camera) + ".json")),
										  pinhole(500.0 + 10.0 * camera)));
	}
	writeText(dir / ".cam9.json", "not a document");
	writeText(dir / "notes.txt", "the rig, as captured");

	const flowview::FileResult<std::vector<camera::CameraModel>> loaded = flowview::loadRigModels(dir);
	INFO(loaded.problems.size() << " problems");
	REQUIRE(loaded.value.has_value());
	CHECK(loaded.problems.empty());
	REQUIRE(loaded.value->size() == 6);
	for (std::size_t i = 0; i < 6; ++i)
		CHECK((*loaded.value)[i].intrinsics().fx == 500.0 + 10.0 * static_cast<double>(i));

	// The cli's `--models` reads the same folder to the same cameras, in the same order.
	const std::optional<flow::PortValue> bound = builtIn().bind(typeid(std::vector<camera::CameraModel>), dir.string());
	REQUIRE(bound.has_value());
	const auto& models = bound->get<std::vector<camera::CameraModel>>();
	REQUIRE(models.size() == 6);
	for (std::size_t i = 0; i < 6; ++i)
		CHECK(models[i].intrinsics().fx == (*loaded.value)[i].intrinsics().fx);
}

TEST_CASE("a rig's models are refused when any one is, with every bad one named", "[flowview][camerafiles]")
{
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-rig-refused");
	REQUIRE(flowview::saveCameraModel(core::Uri::fromPath(dir / "cam0.json"), pinhole(500.0)));
	writeMisspelled(dir / "cam0.json", dir / "cam1.json");
	REQUIRE(flowview::saveCameraModel(core::Uri::fromPath(dir / "cam2.json"), pinhole(520.0)));
	writeText(dir / "cam3.json", "{");

	const flowview::FileResult<std::vector<camera::CameraModel>> loaded = flowview::loadRigModels(dir);
	CHECK_FALSE(loaded.value.has_value());
	CHECK(named(loaded.problems, {(dir / "cam1.json").string(), "kk2"}));
	CHECK(named(loaded.problems, {(dir / "cam3.json").string()}));
	CHECK_FALSE(named(loaded.problems, {(dir / "cam0.json").string()}));
	CHECK_FALSE(builtIn().bind(typeid(std::vector<camera::CameraModel>), dir.string()).has_value());
}

TEST_CASE("a rig folder with no cameras in it says why", "[flowview][camerafiles]")
{
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-rig-empty");

	const fs::path absent = dir / "absent";
	CHECK(flowview::loadRigModels(absent).problems == std::vector<std::string>{absent.string() + ": not a folder"});
	CHECK(flowview::openRigFootage(absent).problems == std::vector<std::string>{absent.string() + ": not a folder"});

	// A note is not a model, and a hidden entry is not a camera.
	const fs::path notes = dir / "notes";
	fs::create_directories(notes);
	writeText(notes / "readme.txt", "models go here");
	writeText(notes / ".DS_Store", "");
	const flowview::FileResult<std::vector<camera::CameraModel>> models = flowview::loadRigModels(notes);
	CHECK_FALSE(models.value.has_value());
	CHECK(models.problems == std::vector<std::string>{notes.string() + ": holds no camera-model documents (.json)"});

	const fs::path hidden = dir / "hidden";
	fs::create_directories(hidden);
	writeText(hidden / ".DS_Store", "");
	const flowview::FileResult<std::vector<media::FrameSequence>> footage = flowview::openRigFootage(hidden);
	CHECK_FALSE(footage.value.has_value());
	CHECK(footage.problems == std::vector<std::string>{hidden.string() + ": holds no footage"});
}

TEST_CASE("a rig's footage opens a sequence per camera by name, and an entry that is not footage is named", "[flowview][camerafiles]")
{
	// Each camera's stills are a width of their own, so the order shows in the specs as well as the
	// sources. The hidden entry beside them is skipped: opened, it would refuse the rig.
	ensureRegistered();
	const fs::path rig = freshDir("camerafiles-rig-footage");
	for (const int camera : {2, 0, 1})
		writeStills(rig / ("cam" + std::to_string(camera)), 16 + 8 * camera);
	writeText(rig / ".DS_Store", "");

	const flowview::FileResult<std::vector<media::FrameSequence>> opened = flowview::openRigFootage(rig);
	INFO(opened.problems.size() << " problems");
	REQUIRE(opened.value.has_value());
	REQUIRE(opened.value->size() == 3);
	for (std::size_t i = 0; i < 3; ++i)
	{
		const media::FrameSequence& footage = (*opened.value)[i];
		CHECK(footage.size() == 2);
		CHECK(footage.spec().extent.x == 16 + 8 * static_cast<int>(i));
		CHECK(footage.frame(0).source.toString().find("cam" + std::to_string(i)) != std::string::npos);
	}

	// The cli's `--footage` opens the same cameras in the same order.
	const std::optional<flow::PortValue> bound = builtIn().bind(typeid(std::vector<media::FrameSequence>), rig.string());
	REQUIRE(bound.has_value());
	const auto& footage = bound->get<std::vector<media::FrameSequence>>();
	REQUIRE(footage.size() == 3);
	for (std::size_t i = 0; i < 3; ++i)
		CHECK(footage[i].frame(0).source == (*opened.value)[i].frame(0).source);

	// A note left in the rig is not a camera's footage: the rig is refused, and the note named.
	writeText(rig / "readme.txt", "three cameras");
	const flowview::FileResult<std::vector<media::FrameSequence>> refused = flowview::openRigFootage(rig);
	CHECK_FALSE(refused.value.has_value());
	CHECK(refused.problems == std::vector<std::string>{(rig / "readme.txt").string() + ": could not be opened as footage"});
	CHECK_FALSE(builtIn().bind(typeid(std::vector<media::FrameSequence>), rig.string()).has_value());
}
