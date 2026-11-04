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
#include <lain/camera/capture/location.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/core/uri.h>
#include <lain/flow/portvalue.h>
#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/open.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/io/video/save.h>
#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
		io::video::registerVideoCodecs();
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

// One camera's footage: a folder of `count` stills `width` pixels wide.
static void writeStills(const fs::path& folder, int width, int count = 2)
{
	fs::create_directories(folder);
	for (int i = 0; i < count; ++i)
	{
		const fs::path still = folder / ("frame.000" + std::to_string(i) + ".png");
		REQUIRE(io::image::save(core::Uri::fromPath(still), image::Image(width, 8)));
	}
}

// One camera's footage as a video file: `frames` lossless frames `width` pixels wide. False when this
// build has no video writer.
static bool writeVideo(const fs::path& file, int width, int frames)
{
	media::FrameSpec spec;
	spec.extent = {width, 8};
	spec.pixelFormat = image::PixelFormat::Gray8;
	spec.colorSpace = image::ColorSpace::sRGB;
	spec.rate = {24, 1};
	io::video::VideoWriterOptions options;
	options.codec = io::video::VideoCodec::FFV1;
	const std::unique_ptr<io::video::VideoWriter> writer = io::video::openWriter(core::Uri::fromPath(file), spec, options);
	if (!writer)
		return false;
	for (int i = 0; i < frames; ++i)
		REQUIRE(writer->encode(image::Image(width, 8, image::PixelFormat::Gray8, image::ColorSpace::sRGB)));
	return writer->finish();
}

// What a graph names each camera of a rig's footage: capture::identitiesByLocation, the routine the
// registration nodes name their cameras with.
static std::vector<std::string> cameraNames(const std::vector<media::FrameSequence>& footage)
{
	std::vector<std::string> names;
	for (const camera::capture::CameraIdentity& identity : camera::capture::identitiesByLocation(footage))
		names.push_back(identity.value);
	return names;
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

TEST_CASE("a rig's footage and models pair by camera name, however the names sort as files", "[flowview][camerafiles]")
{
	// As folders, a sorts before a-b; as documents, a-b.json sorts before a.json, since '-' sorts below
	// '.'. Listed by file name, the two cameras took each other's models and nothing said so, since the
	// registration nodes pair a rig's footage and models by position. Each camera's stills are a width of
	// their own and each model a focal length of its own, so a position shows which camera each half is.
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-rig-names");
	const std::vector<std::string> names{"a", "a-b", "b"};
	fs::create_directories(dir / "models");
	for (std::size_t i = 0; i < names.size(); ++i)
	{
		writeStills(dir / "rig" / names[i], 16 + 8 * static_cast<int>(i));
		REQUIRE(flowview::saveCameraModel(core::Uri::fromPath(dir / "models" / (names[i] + ".json")),
										  pinhole(500.0 + 10.0 * static_cast<double>(i))));
	}

	const flowview::FileResult<std::vector<media::FrameSequence>> footage = flowview::openRigFootage(dir / "rig");
	const flowview::FileResult<std::vector<camera::CameraModel>> models = flowview::loadRigModels(dir / "models");
	REQUIRE(footage.value.has_value());
	REQUIRE(models.value.has_value());
	CHECK(cameraNames(*footage.value) == names);
	REQUIRE(models.value->size() == names.size());
	for (std::size_t i = 0; i < names.size(); ++i)
	{
		INFO("camera " << names[i]);
		CHECK((*footage.value)[i].spec().extent.x == 16 + 8 * static_cast<int>(i));
		CHECK((*models.value)[i].intrinsics().fx == 500.0 + 10.0 * static_cast<double>(i));
	}
	CHECK(flowview::unpairedCameras(dir / "rig", dir / "models").empty());

	// The cli's `--footage` and `--models` put them in the same order.
	const flowview::BoundaryBinders binders = builtIn();
	const std::optional<flow::PortValue> boundFootage =
		binders.bind(typeid(std::vector<media::FrameSequence>), (dir / "rig").string());
	const std::optional<flow::PortValue> boundModels =
		binders.bind(typeid(std::vector<camera::CameraModel>), (dir / "models").string());
	REQUIRE(boundFootage.has_value());
	REQUIRE(boundModels.has_value());
	CHECK(cameraNames(boundFootage->get<std::vector<media::FrameSequence>>()) == names);
	const auto& boundList = boundModels->get<std::vector<camera::CameraModel>>();
	REQUIRE(boundList.size() == names.size());
	for (std::size_t i = 0; i < names.size(); ++i)
		CHECK(boundList[i].intrinsics().fx == 500.0 + 10.0 * static_cast<double>(i));
}

TEST_CASE("a camera with no partner in the other folder is named", "[flowview][camerafiles]")
{
	// Listed, never opened, so the entries need hold nothing. A video is named less its extension, as a
	// graph names it, so c.mp4's model is c.json.
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-rig-partners");
	const fs::path rig = dir / "rig";
	const fs::path models = dir / "models";
	fs::create_directories(rig / "a");
	fs::create_directories(rig / "b");
	writeText(rig / "c.mp4", "");
	writeText(rig / ".DS_Store", "");
	fs::create_directories(models);
	for (const char* name : {"a.json", "c.json", "d.json", ".e.json", "notes.txt"})
		writeText(models / name, "{}");

	CHECK(flowview::unpairedCameras(rig, models) ==
		  std::vector<std::string>{
			  "b: footage " + (rig / "b").string() + " has no camera model " + (models / "b.json").string(),
			  "d: camera model " + (models / "d.json").string() + " has no footage named d in " + rig.string()});

	// Named alike, the two pair.
	fs::rename(models / "d.json", models / "b.json");
	CHECK(flowview::unpairedCameras(rig, models).empty());

	// A model named for the video's file is another camera's.
	fs::rename(models / "c.json", models / "c.mp4.json");
	CHECK(flowview::unpairedCameras(rig, models) ==
		  std::vector<std::string>{
			  "c: footage " + (rig / "c.mp4").string() + " has no camera model " + (models / "c.json").string(),
			  "c.mp4: camera model " + (models / "c.mp4.json").string() + " has no footage named c.mp4 in " + rig.string()});

	// A folder that cannot be listed is the loaders' to report, not this.
	CHECK(flowview::unpairedCameras(dir / "absent", models).empty());
	CHECK(flowview::unpairedCameras(rig, dir / "absent").empty());
}

TEST_CASE("a camera of one still is named by the folder holding it", "[flowview][camerafiles]")
{
	// A folder of stills opens as ONE source named by the folder (io::image::open), so a camera is named
	// by its folder however many stills it holds. One still is not taken for a video and named by its
	// file, so a camera of one frame pairs with a model or a truth named for its folder.
	ensureRegistered();
	const fs::path rig = freshDir("camerafiles-rig-one-still");
	writeStills(rig / "cam0", 16, 1);
	writeStills(rig / "cam1", 24, 1);
	writeStills(rig / "cam2", 32, 3);

	const flowview::FileResult<std::vector<media::FrameSequence>> opened = flowview::openRigFootage(rig);
	REQUIRE(opened.value.has_value());
	REQUIRE(opened.value->size() == 3);
	CHECK((*opened.value)[0].size() == 1);
	CHECK((*opened.value)[2].size() == 3);
	CHECK(cameraNames(*opened.value) == std::vector<std::string>{"cam0", "cam1", "cam2"});
	// One camera alone, as a calibration node names its camera.
	CHECK(cameraNames({(*opened.value)[0]}) == std::vector<std::string>{"cam0"});
}

TEST_CASE("two footage entries that would be one camera are named, every one", "[flowview][camerafiles]")
{
	// Listed, never opened. A folder cam0 and a video cam0.mp4 are both camera cam0, and so are two videos
	// cam0.mov and cam0.mp4: a model cam0.json could not say which it is for.
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-rig-shared");
	const fs::path rig = dir / "rig";
	const fs::path models = dir / "models";
	fs::create_directories(rig / "cam0");
	for (const char* name : {"cam0.mov", "cam0.mp4", "cam1.mp4"})
		writeText(rig / name, "");
	fs::create_directories(models);
	for (const char* name : {"cam0.json", "cam1.json"})
		writeText(models / name, "{}");

	CHECK(flowview::unpairedCameras(rig, models) ==
		  std::vector<std::string>{"cam0: " + (rig / "cam0").string() + ", " + (rig / "cam0.mov").string() + " and " +
								   (rig / "cam0.mp4").string() +
								   " would each be camera cam0, and a model cam0.json could not say which it is for"});
}

TEST_CASE("a video camera is named less its extension, and pairs with its model by that name", "[flowview][camerafiles]")
{
	// The repo owner's rule: a camera is named the same whatever it was captured to, so a video cam1.mkv
	// is camera cam1 and its model cam1.json, as a folder of stills cam1 would be.
	ensureRegistered();
	const fs::path dir = freshDir("camerafiles-rig-video");
	const fs::path rig = dir / "rig";
	const fs::path models = dir / "models";
	writeStills(rig / "cam0", 16);
	if (!writeVideo(rig / "cam1.mkv", 24, 3))
		SKIP("this build has no video writer");
	fs::create_directories(models);
	REQUIRE(flowview::saveCameraModel(core::Uri::fromPath(models / "cam0.json"), pinhole(500.0)));
	REQUIRE(flowview::saveCameraModel(core::Uri::fromPath(models / "cam1.json"), pinhole(510.0)));

	const flowview::FileResult<std::vector<media::FrameSequence>> footage = flowview::openRigFootage(rig);
	const flowview::FileResult<std::vector<camera::CameraModel>> loaded = flowview::loadRigModels(models);
	REQUIRE(footage.value.has_value());
	REQUIRE(loaded.value.has_value());
	REQUIRE(footage.value->size() == 2);
	REQUIRE(loaded.value->size() == 2);
	CHECK(cameraNames(*footage.value) == std::vector<std::string>{"cam0", "cam1"});
	CHECK((*footage.value)[1].size() == 3);
	CHECK((*footage.value)[1].frame(0).source.extension() == "mkv"); // the video, opened as one
	CHECK((*loaded.value)[0].intrinsics().fx == 500.0);
	CHECK((*loaded.value)[1].intrinsics().fx == 510.0);
	CHECK(flowview::unpairedCameras(rig, models).empty());

	// Beside a folder of stills named cam1, the video would be the same camera: refused, both named. A
	// graph that opens them another way keeps them apart by their whole names.
	writeStills(rig / "cam1", 24);
	const std::string shared = "cam1: " + (rig / "cam1").string() + " and " + (rig / "cam1.mkv").string() +
							   " would each be camera cam1, and a model cam1.json could not say which it is for";
	CHECK(flowview::unpairedCameras(rig, models) == std::vector<std::string>{shared});
	const flowview::FileResult<std::vector<media::FrameSequence>> refused = flowview::openRigFootage(rig);
	CHECK_FALSE(refused.value.has_value());
	CHECK(refused.problems == std::vector<std::string>{shared});
	CHECK_FALSE(builtIn().bind(typeid(std::vector<media::FrameSequence>), rig.string()).has_value());

	std::vector<media::FrameSequence> apart;
	for (const char* name : {"cam0", "cam1", "cam1.mkv"})
	{
		std::optional<media::FrameSequence> one = io::sequence::open(core::Uri::fromPath(rig / name));
		REQUIRE(one.has_value());
		apart.push_back(std::move(*one));
	}
	CHECK(cameraNames(apart) == std::vector<std::string>{"cam0", "cam1", "cam1.mkv"});
}
