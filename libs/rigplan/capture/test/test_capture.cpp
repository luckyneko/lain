// lain::rigplan::capture: a rig plan's synthetic capture on disk. Everything a capture can refuse is
// found before a byte is written; the truth is the realisation, read back through its own readers; the
// noise is the same however many threads record it; and the memory is bounded however many cameras the
// rig has. Boards are drawn by a stand-in renderer, so every build draws them.
//
// The [gpu] cases run on every kind of device the machine offers (gpu.h). The refusal cases never reach
// a device, so they hand writeCapture an invalid one.

#include "gpu.h"
#include "standinboard.h"
#include "testplan.h"

#include <lain/camera/serialize/cameramodel.h>
#include <lain/camera/serialize/registeredrig.h>
#include <lain/core/sha256.h>
#include <lain/io/data/codecs.h>
#include <lain/io/data/load.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/open.h>
#include <lain/io/sequence/openers.h>
#include <lain/rigplan/capture.h>
#include <lain/rigplan/sensor.h>
#include <lain/rigplan/serialize/capture.h>
#include <lain/rigplan/serialize/truth.h>
#include <lain/rigplan/truth.h>
#include <lain/string/format.h>
#include <lain/testing/scratch.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace lain;
using namespace lain::rigplan;
namespace fs = std::filesystem;

static void registerEverything()
{
	static const bool once = []
	{
		io::image::registerImageCodecs();
		io::data::registerDataCodecs();
		io::sequence::registerSequenceOpeners();
		registerStandIn();
		return true;
	}();
	(void)once;
}

static const core::Uuid bulbId = testplan::id("0193a6f0-0000-7000-8000-00000000f001");
static const core::Uuid ceilingId = testplan::id("0193a6f0-0000-7000-8000-00000000a003");

// The test plan at a tenth of its resolution (128 x 72, the same field of view), in a room around it
// with a procedural floor, lit by a bulb in the ceiling, its board crossing in front of the pole.
// cam0 and cam1 are RGB through the barrel lens and written as 8-bit sRGB PNG; cam2 is a grey pinhole
// written as 16-bit linear TIFF, so both formats and both kinds of sensor are written.
static RigPlan smallPlan(std::size_t frames = 3)
{
	RigPlan plan = testplan::plan();
	SensorSpecification& hd = plan.specifications.sensors[0];
	hd.image = {128, 72};
	hd.pixelPitch = core::Length::from<core::Length::Micrometres>(20.0);
	SensorSpecification grey = hd;
	grey.name = "grey";
	grey.sensorModel.channels = SensorChannels::Grey;
	grey.sensorModel.output = {OutputFormat::Tiff, 16, image::ColorSpace::Linear, std::nullopt};
	plan.specifications.sensors.push_back(grey);
	LensSpecification pinhole = testplan::lens("pinhole");
	pinhole.distortion = camera::NoDistortion{};
	plan.specifications.lenses.push_back(pinhole);
	plan.specifications.cameras.push_back({"grey-pinhole", "grey", "pinhole"});
	plan.cameras[2].camera = "grey-pinhole";

	Room room;
	room.centre = {2.0, 3.0};
	room.size = {10.0, 8.0, 3.2};
	room.floor.material.surface = image::ProceduralTexture{5, 0.4};
	plan.room = room;
	LightSpecification bulb;
	bulb.name = "bulb";
	bulb.intensity = 4;
	plan.specifications.lights.push_back(bulb);
	Mount ceiling;
	ceiling.id = ceilingId;
	ceiling.name = "ceiling";
	ceiling.kind = FixedPoint{{2.0, 3.0, 3.0}};
	plan.mounts.push_back(ceiling);
	PlacedLight light;
	light.id = bulbId;
	light.name = "bulb";
	light.light = "bulb";
	light.mount = ceilingId;
	plan.lights.push_back(light);

	plan.boards.push_back(testplan::board());
	plan.timeline.frames = frames;
	return plan;
}

// Every file under `folder`, relative and spelled generic, sorted.
static std::vector<std::string> filesUnder(const fs::path& folder)
{
	std::vector<std::string> files;
	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(folder))
	{
		if (entry.is_regular_file())
			files.push_back(fs::relative(entry.path(), folder).generic_string());
	}
	std::sort(files.begin(), files.end());
	return files;
}

static data::Value document(const fs::path& path)
{
	const std::optional<data::Value> value = io::data::load(core::Uri::fromPath(path));
	REQUIRE(value.has_value());
	return *value;
}

// The digest of every frame's decoded bytes, per file: what "the same noise" means.
static std::map<std::string, std::string> frameDigests(const fs::path& folder)
{
	std::map<std::string, std::string> digests;
	for (const std::string& file : filesUnder(folder / "rig"))
	{
		const std::optional<image::Image> frame = io::image::load(core::Uri::fromPath(folder / "rig" / file));
		REQUIRE(frame.has_value());
		digests[file] = core::sha256(frame->data(), frame->byteSize()).toString();
	}
	return digests;
}

static bool mentions(const std::vector<std::string>& problems, const std::string& text)
{
	return std::any_of(problems.begin(), problems.end(), [&](const std::string& p)
					   { return p.find(text) != std::string::npos; });
}

static std::size_t entriesIn(const fs::path& folder)
{
	return std::size_t(std::distance(fs::directory_iterator(folder), fs::directory_iterator{}));
}

TEST_CASE("a capture writes its frames, its truth and its manifest in the layout M9 binds", "[rigplan][capture][gpu]")
{
	registerEverything();
	acm::Device& device = render::testing::device();
	const RigPlan plan = smallPlan();
	CaptureOptions options;
	options.out = testing::scratchPath("capture");
	options.seed = 11;
	options.noise = 1.0;
	const CaptureReport report = writeCapture(plan, device, options);
	INFO(report.problems.size() << " problems, the first: " << (report.problems.empty() ? "" : report.problems[0]));
	REQUIRE(report.problems.empty());
	CHECK(report.framesWritten == 9);
	CHECK(report.peakRayTables == 1);
	CHECK(report.peakInFlight >= 1);
	CHECK(report.peakInFlight <= report.maxInFlight);
	CHECK(report.maxInFlight == 1); // no pool in this case: one a worker, at least one

	const std::vector<std::string> expected = {
		"capture.json",
		"nominal/cam0.json",
		"nominal/cam1.json",
		"nominal/cam2.json",
		"rig/cam0/frame.0000.png",
		"rig/cam0/frame.0001.png",
		"rig/cam0/frame.0002.png",
		"rig/cam1/frame.0000.png",
		"rig/cam1/frame.0001.png",
		"rig/cam1/frame.0002.png",
		"rig/cam2/frame.0000.tiff",
		"rig/cam2/frame.0001.tiff",
		"rig/cam2/frame.0002.tiff",
		"truth/board.json",
		"truth/models/cam0.json",
		"truth/models/cam1.json",
		"truth/models/cam2.json",
		"truth/poses.json",
		"truth/rig.json",
	};
	CHECK(filesUnder(options.out) == expected);

	// The frames: each camera's format and tag, and footage M9 opens, three frames a camera.
	for (const char* name : {"cam0", "cam1", "cam2"})
	{
		const bool grey = std::string(name) == "cam2";
		const std::optional<media::FrameSequence> footage = io::sequence::open(core::Uri::fromPath(options.out / "rig" / name));
		REQUIRE(footage.has_value());
		CHECK(footage->size() == 3);
		const std::optional<image::Image> frame =
			io::image::load(core::Uri::fromPath(options.out / "rig" / name / (grey ? "frame.0001.tiff" : "frame.0001.png")));
		REQUIRE(frame.has_value());
		CHECK(frame->extent() == math::Vec2i{128, 72});
		CHECK(frame->pixelFormat() == (grey ? image::PixelFormat::Gray16 : image::PixelFormat::RGB8));
		CHECK(frame->colorSpace() == (grey ? image::ColorSpace::Linear : image::ColorSpace::sRGB));
	}

	// The truth is the realisation the capture was rendered with, read back through its own readers.
	const RigPlan captured = capturedPlan(plan, options);
	const Realisation realisation = *realise(captured).realisation;
	const PosesRead poses = posesFromValue(document(options.out / "truth" / "poses.json"));
	REQUIRE(poses.poses.has_value());
	REQUIRE(poses.poses->size() == 3);
	for (std::size_t c = 0; c < 3; ++c)
	{
		const RealisedCamera& camera = realisation.cameras[c];
		const CameraPose& pose = (*poses.poses)[c];
		CHECK(pose.id == camera.id);
		CHECK(pose.name == camera.name);
		CHECK(math::length(pose.planFromCamera.translation() - camera.planFromCamera.translation()) < 1e-12);
		const camera::CameraModelRead model = camera::cameraModelFromValue(document(options.out / "truth" / "models" / (camera.name + ".json")));
		REQUIRE(model.model.has_value());
		CHECK(model.model->parameters().intrinsics.fx == camera.model.parameters().intrinsics.fx);
		CHECK(model.model->parameters().intrinsics.cx == camera.model.parameters().intrinsics.cx);
		const camera::CameraModelRead nominal = camera::cameraModelFromValue(document(options.out / "nominal" / (camera.name + ".json")));
		REQUIRE(nominal.model.has_value());
		// The capture is realised at full intrinsic scale, so the truth parts from the datasheet.
		CHECK(nominal.model->parameters().intrinsics.fx != camera.model.parameters().intrinsics.fx);
	}
	const camera::registration::RegisteredRigRead rig = camera::registration::registeredRigFromValue(document(options.out / "truth" / "rig.json"));
	REQUIRE(rig.rig.has_value());
	CHECK(rig.rig->reference.value == "cam0");
	CHECK(rig.rig->cameras.size() == 3);
	const BoardTruthRead board = boardTruthFromValue(document(options.out / "truth" / "board.json"));
	REQUIRE(board.truth.has_value());
	CHECK(board.truth->size() == 3); // one board, three frames

	// The manifest: the plan as rendered, which alone reproduces the capture, and each camera's output
	// and record, its imported model the nominal one.
	const CaptureManifestRead manifest = captureManifestFromValue(document(options.out / "capture.json"), options.out);
	INFO((manifest.problems.empty() ? "" : manifest.problems[0]));
	REQUIRE(manifest.manifest.has_value());
	CHECK(manifest.manifest->plan.seed == 11);
	CHECK(manifest.manifest->plan.scales.noise == 1.0);
	CHECK(manifest.manifest->shutter == Shutter::Global);
	CHECK(manifest.manifest->deviceKind == (device.deviceInfo().type == acm::PhysicalDeviceType::Cpu ? "cpu" : "hardware"));
	REQUIRE(manifest.manifest->cameras.size() == 3);
	CHECK(manifest.manifest->cameras[2].output.format == OutputFormat::Tiff);
	const camera::CaptureRecord& record = manifest.manifest->cameras[0].record;
	CHECK(record.stream.width == 128);
	CHECK(record.stream.format == "RGB8");
	REQUIRE(record.importedModel.has_value());
	const camera::CameraModelRead nominal0 = camera::cameraModelFromValue(document(options.out / "nominal" / "cam0.json"));
	CHECK(record.importedModel->parameters.intrinsics.fx == nominal0.model->parameters().intrinsics.fx);
}

TEST_CASE("a capture's noise is the same however many threads record it, and its seed decides it", "[rigplan][capture][gpu]")
{
	registerEverything();
	acm::Device& device = render::testing::device();
	const RigPlan plan = smallPlan(2);
	CaptureOptions options;
	options.noise = 1.0;
	options.seed = 21;

	options.out = testing::scratchPath("inline");
	REQUIRE(writeCapture(plan, device, options).problems.empty()); // no pool: every frame recorded inline
	const std::map<std::string, std::string> inlineDigests = frameDigests(options.out);
	std::map<std::string, std::string> pooled;
	{
		const testing::ThreadPool pool(4);
		options.out = testing::scratchPath("pooled");
		REQUIRE(writeCapture(plan, device, options).problems.empty());
		pooled = frameDigests(options.out);
		options.seed = 22;
		options.out = testing::scratchPath("reseeded");
		REQUIRE(writeCapture(plan, device, options).problems.empty());
	}
	CHECK(inlineDigests.size() == 6);
	CHECK(pooled == inlineDigests);
	const std::map<std::string, std::string> reseeded = frameDigests(options.out);
	REQUIRE(reseeded.size() == inlineDigests.size());
	for (const auto& [file, digest] : reseeded)
		CHECK(digest != inlineDigests.at(file));
}

TEST_CASE("a camera's frames do not depend on its place in the rig plan", "[rigplan][capture][gpu]")
{
	// Its draws and its noise are keyed by its identity (ADR-0029), so listing the cameras the other way
	// round records every camera's frames to the byte.
	registerEverything();
	acm::Device& device = render::testing::device();
	RigPlan plan = smallPlan(2);
	CaptureOptions options;
	options.noise = 1.0;
	options.deviation = 1.0;
	options.out = testing::scratchPath("forward");
	REQUIRE(writeCapture(plan, device, options).problems.empty());
	const std::map<std::string, std::string> forward = frameDigests(options.out);
	std::reverse(plan.cameras.begin(), plan.cameras.end());
	plan.reference = testplan::cam0Id; // the truth's rig stays relative to cam0
	options.out = testing::scratchPath("reversed");
	REQUIRE(writeCapture(plan, device, options).problems.empty());
	CHECK(frameDigests(options.out) == forward);
}

// A hundred cameras on one bracket, 16 x 12 each, recorded by a sensor whose writes are slowed down so
// that frames pile up behind the device.
static RigPlan hundredCameras()
{
	RigPlan plan = testplan::plan();
	plan.specifications.sensors[0].image = {16, 12};
	plan.specifications.sensors[0].pixelPitch = core::Length::from<core::Length::Micrometres>(160.0);
	plan.cameras.clear();
	plan.lights.clear();
	Room room;
	room.centre = {2.0, 3.0};
	room.size = {10.0, 8.0, 3.2};
	plan.room = room;
	for (int i = 0; i < 100; ++i)
	{
		const std::string text = string::format("0193a6f0-0000-7000-8000-0000000d{:04d}", i);
		PlacedCamera camera = testplan::camera(testplan::id(text.c_str()), string::format("cam{:03d}", i), testplan::wallId,
											   {0.0, 0.1, -0.01 * i}, {double(i), -10.0, 0.0});
		plan.cameras.push_back(camera);
	}
	plan.timeline.frames = 3;
	return plan;
}

TEST_CASE("a capture of a hundred cameras holds one ray table and at most its frames in flight", "[rigplan][capture][gpu][scale]")
{
	registerEverything();
	acm::Device& device = render::testing::device();
	const RigPlan plan = hundredCameras();
	REQUIRE(check(plan).empty());
	const testing::ThreadPool pool(4);
	CaptureOptions options;
	options.out = testing::scratchPath("hundred");
	options.maxInFlight = 4;
	std::atomic<std::size_t> written{0};
	// A slow disk: each write takes 20 ms, far longer than a 16 x 12 capture, so without back-pressure
	// every frame would be in flight at once.
	options.onFrameWritten = [&](const WrittenFrame&)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		++written;
	};
	const CaptureReport report = writeCapture(plan, device, options);
	REQUIRE(report.problems.empty());
	CHECK(report.framesWritten == 300);
	CHECK(written.load() == 300);
	CHECK(report.peakInFlight == 4); // reached, so the bound is what held it
	CHECK(report.peakRayTables == 1);
	CHECK(entriesIn(options.out / "rig") == 100);
}

TEST_CASE("a capture a write fails in leaves no manifest", "[rigplan][capture][gpu]")
{
	registerEverything();
	acm::Device& device = render::testing::device();
	const RigPlan plan = smallPlan();
	CaptureOptions options;
	options.out = testing::scratchPath("failing");
	options.maxInFlight = 1;
	// Once cam0's first frame is on disk, its folder is replaced by a file, so its next frames cannot be.
	options.onFrameWritten = [&](const WrittenFrame& frame)
	{
		if (frame.camera == "cam0" && frame.frame == 0)
		{
			fs::remove_all(options.out / "rig" / "cam0");
			std::ofstream(options.out / "rig" / "cam0") << "in the way";
		}
	};
	const CaptureReport report = writeCapture(plan, device, options);
	CHECK_FALSE(report.refused);
	CHECK(mentions(report.problems, "frame.0001.png"));
	CHECK(fs::exists(options.out / "truth" / "rig.json")); // the truth went first
	CHECK_FALSE(fs::exists(options.out / "capture.json"));
}

// --- refusals: found before a byte is written, with no device needed --------------------------

static CaptureReport refusal(const RigPlan& plan, CaptureOptions options)
{
	registerEverything();
	acm::Device none;
	if (options.out.empty())
		options.out = testing::scratchPath("refused");
	const CaptureReport report = writeCapture(plan, none, options);
	CHECK(report.refused);
	CHECK_FALSE(report.problems.empty());
	return report;
}

TEST_CASE("a capture is refused before anything is written", "[rigplan][capture]")
{
	SECTION("into a folder that is not empty, which is left as it was")
	{
		CaptureOptions options;
		options.out = testing::scratchPath("occupied");
		fs::create_directories(options.out);
		std::ofstream(options.out / "notes.txt") << "mine";
		const CaptureReport report = refusal(smallPlan(), options);
		CHECK(mentions(report.problems, "is not empty"));
		CHECK(filesUnder(options.out) == std::vector<std::string>{"notes.txt"});
	}
	SECTION("of a rig plan check() refuses, which creates no folder")
	{
		RigPlan plan = smallPlan();
		plan.timeline.frames = 0;
		CaptureOptions options;
		options.out = testing::scratchPath("unchecked");
		refusal(plan, options);
		CHECK_FALSE(fs::exists(options.out));
	}
	SECTION("with a deviation outside 0 to 1, which check() refuses of the captured plan")
	{
		CaptureOptions options;
		options.deviation = 1.5;
		refusal(smallPlan(), options);
	}
	SECTION("of an asset that will not load, naming it")
	{
		RigPlan plan = smallPlan();
		Prop prop;
		prop.id = testplan::id("0193a6f0-0000-7000-8000-00000000e009");
		prop.name = "statue";
		prop.mesh = core::Uri::fromPath(testing::scratchPath("missing", ".obj"));
		plan.props.push_back(prop);
		CHECK(mentions(refusal(plan, {}).problems, "statue"));
	}
	SECTION("of a curve its codec cannot state, asked of the codec")
	{
		RigPlan plan = smallPlan();
		plan.specifications.sensors[0].sensorModel.output.curve = image::ColorSpace::BT709; // PNG states sRGB and gamma, not BT.709
		const CaptureReport report = refusal(plan, {});
		CHECK(mentions(report.problems, "camera 'cam0': png cannot hold"));
	}
	SECTION("of names a rig folder would hide or pair wrongly with their models")
	{
		RigPlan plan = smallPlan();
		plan.cameras[0].name = "a";
		plan.cameras[1].name = "a-b";
		plan.cameras[2].name = ".hidden";
		const CaptureReport report = refusal(plan, {});
		CHECK(mentions(report.problems, "cameras 'a' and 'a-b' sort one way as folders"));
		CHECK(mentions(report.problems, "camera '.hidden': a name starting with '.'"));
	}
	SECTION("of a camera whose ray table cannot be built, naming it")
	{
		CaptureOptions options;
		options.settings.interpolationTolerance = 1e-12; // no corner lattice interpolates a barrel lens this well
		const CaptureReport report = refusal(smallPlan(), options);
		CHECK(mentions(report.problems, "camera 'cam0'"));
	}
	SECTION("with no frame ever allowed in flight")
	{
		CaptureOptions options;
		options.maxInFlight = 0;
		CHECK(mentions(refusal(smallPlan(), options).problems, "maxInFlight"));
	}
}

// --- the size estimate: no device ---------------------------------------------------------------

TEST_CASE("a capture's size is estimated between a noisy flat floor and the uncompressed ceiling", "[rigplan][capture][estimate]")
{
	registerEverything();
	const RigPlan plan = smallPlan();
	CaptureOptions options;

	SECTION("the ceiling is every frame uncompressed, with its file's overhead")
	{
		const SizeEstimate estimate = estimateSize(plan, options);
		REQUIRE(estimate.problems.empty());
		REQUIRE(estimate.cameras.size() == 3);
		CHECK(estimate.cameras[0].raw == 3u * (128u * 72u * 3u + estimate.cameras[0].overhead));
		CHECK(estimate.cameras[2].raw == 3u * (128u * 72u * 2u + estimate.cameras[2].overhead));
		for (const CameraEstimate& camera : estimate.cameras)
			CHECK(camera.floor <= camera.raw);
		CHECK(estimate.raw == estimate.cameras[0].raw + estimate.cameras[1].raw + estimate.cameras[2].raw);
		CHECK(estimate.frames == 3);
	}
	SECTION("without noise a flat picture costs almost nothing but its file's overhead")
	{
		options.noise = 0.0;
		const SizeEstimate estimate = estimateSize(plan, options);
		REQUIRE(estimate.problems.empty());
		for (const CameraEstimate& camera : estimate.cameras)
		{
			INFO(camera.camera << ": floor " << camera.floor << ", overhead " << camera.overhead << ", raw " << camera.raw);
			CHECK_FALSE(camera.noiseDefeatsCompression);
		}
		CHECK(estimate.cameras[0].floor * 10 < estimate.cameras[0].raw);
		// A 16-bit TIFF states its curve as a transfer function, a 16-bit code for each of its 65536
		// codes: 128 kB in every file, here seven times the pixels themselves.
		CHECK(estimate.cameras[2].overhead > 131072);
		CHECK(estimate.cameras[2].floor > 3u * 128u * 72u * 2u);
		CHECK(estimate.toString().find("noise defeats compression") == std::string::npos);
	}
	SECTION("with a shallow well's noise, it says compression is defeated")
	{
		RigPlan noisy = plan;
		noisy.specifications.sensors[0].sensorModel.fullWellElectrons = 200; // 7% of full scale at half of it
		options.noise = 1.0;
		const SizeEstimate estimate = estimateSize(noisy, options);
		REQUIRE(estimate.problems.empty());
		CHECK(estimate.cameras[0].noiseDefeatsCompression);
		CHECK(estimate.cameras[1].noiseDefeatsCompression);
		CHECK(estimate.toString().find("noise defeats compression for cam0, cam1") != std::string::npos);
	}
}

TEST_CASE("a camera's floor, measured on a tile, is near a whole flat frame's encoded size", "[rigplan][capture][estimate]")
{
	registerEverything();
	// Cameras larger than the tile, so the floor is scaled up from it: an 8-bit PNG, and a 16-bit TIFF
	// whose every file carries a 128 kB transfer function that must not be scaled with the pixels.
	RigPlan plan = smallPlan(1);
	for (SensorSpecification& sensor : plan.specifications.sensors)
	{
		sensor.image = {1024, 576};
		sensor.pixelPitch = core::Length::from<core::Length::Micrometres>(2.5);
		sensor.sensorModel.fullWellElectrons = 2000;
	}
	CaptureOptions options;
	options.noise = 1.0;
	const SizeEstimate estimate = estimateSize(plan, options);
	REQUIRE(estimate.problems.empty());

	for (std::size_t c : {std::size_t{0}, std::size_t{2}})
	{
		// The same flat light over the whole frame, recorded and encoded as the capture would.
		const SensorSpecification& sensor = *findSensor(plan.specifications, findCamera(plan.specifications, plan.cameras[c].camera)->sensor);
		const SensorModel& model = sensor.sensorModel;
		const Recorder recorder = *Recorder::create(model);
		image::Image light(1024, 576, image::PixelFormat::RGBA32F, image::ColorSpace::Linear);
		float* texel = reinterpret_cast<float*>(light.data());
		for (std::size_t i = 0; i < light.pixelCount(); ++i, texel += 4)
		{
			texel[0] = texel[1] = texel[2] = float(0.5 / model.gain);
			texel[3] = 1.0f;
		}
		const image::Image recorded = recorder.record(light, {plan.seed, plan.cameras[c].id, 0}, 1.0);
		const std::optional<memory::Buffer> whole = io::image::encode(model.output.format == OutputFormat::Tiff ? "tiff" : "png", recorded);
		REQUIRE(whole.has_value());
		const double ratio = double(estimate.cameras[c].floor) / double(whole->size());
		INFO(plan.cameras[c].name << ": floor " << estimate.cameras[c].floor << " bytes against a whole frame's " << whole->size());
		CHECK(ratio > 0.9);
		CHECK(ratio < 1.1);
	}
}
