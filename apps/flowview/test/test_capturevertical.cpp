// The synthetic-capture vertical (WORK.md M15 slice 7): a committed rig plan rendered, then registered
// and scored as a user does it, with nothing in between that a user would not have.
//
//   1. apps/rigplan/examples/three-poles.json, read as `rigplan render` reads it, is rendered with
//      lain::rigplan::writeCapture on every kind of device the machine offers (gpu.h);
//   2. flowview's committed register.json registers the capture's rig/ footage twice through runGraph,
//      once with its truth/models and once with its nominal/ models: an uncalibrated rig and a
//      perfectly calibrated one from the same pixels (ADR-0029, "a binding choice, not a regeneration");
//   3. compare-rigs.json scores each against the capture's truth/rig.json.
//
// With its true models the rig comes back near-exactly, though installation deviation moved every
// camera; with its nominal ones, measurably worse. The truth is relative to the rig plan's reference
// and the registration chooses its own, so the comparison also derives the truth again (slice 6).
//
// And the same for registration without a target: apps/rigplan/examples/two-methods.json rendered,
// register-targetless.json run with its truth/models, compare-rigs.json scoring the rig against its
// truth with a scale fitted, and the same footage copied elsewhere on disk registering the same, byte for
// byte (capture/location.h). Both run in Release only.
//
// Built only where a board renderer is, since both plans wave a board. Without a refiner the same
// captures register to a report naming the one it is missing.

#include "camerafiles.h"
#include "clibinders.h"
#include "graphio.h"
#include "runmode.h"
#include "scene.h"

#include <lain/camera/backends.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/comparison.h>
#include <lain/camera/refinement/refiner.h>
#include <lain/camera/registration/report.h>
#include <lain/core/factory.h>
#include <lain/core/uri.h>
#include <lain/flow/node.h>
#include <lain/io/image/codecs.h>
#include <lain/io/mesh/codecs.h>
#include <lain/io/sequence/openers.h>
#include <lain/render/testing/gpu.h>
#include <lain/rigplan/capture.h>
#include <lain/rigplan/testing/capture.h>
#include <lain/rigplan/testing/exampleplan.h>
#include <lain/testing/scratch.h>
#include <lain/testing/skip.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

using namespace lain;
namespace fs = std::filesystem;

// What `rigplan render` registers (the frames, the meshes a plan names, the board renderer) and what
// `flowview run` does (the documents, the sequences a binder opens).
static void registerEverything()
{
	static const bool once = []
	{
		io::image::registerImageCodecs();
		io::mesh::registerMeshCodecs();
		io::sequence::registerSequenceOpeners();
		camera::registerCameraBackends();
		flowview::registerSceneSerialization(); // the data codecs among it
		return true;
	}();
	(void)once;
}

static std::string readText(const fs::path& file)
{
	std::ifstream stream(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// A rig document, read by the reader `--rig` binds one with.
static std::optional<camera::registration::RegisteredRig> readRig(const fs::path& file)
{
	const flowview::FileResult<camera::registration::RegisteredRig> read =
		flowview::loadRegisteredRig(core::Uri::fromPath(file));
	INFO("problems: " << read.problems.size() << (read.problems.empty() ? "" : ", the first " + read.problems.front()));
	CHECK(read.problems.empty());
	return read.value;
}

// A committed flowview example document.
static std::string flowviewExample(const std::string& name)
{
	return (fs::path(LAIN_FLOWVIEW_EXAMPLES_DIR) / (name + ".json")).string();
}

// What one registration of the capture wrote: its report's line, and the rig document when there is one.
struct Registration
{
	std::string report;
	fs::path rigFile;
};

// `flowview run -g <document>.json --footage <capture>/rig --models <capture>/<models> --report ... --rig ...`,
// `document` register or register-targetless.
static Registration registerCapture(const std::string& document, const fs::path& capture, const std::string& models,
									const fs::path& into, const core::Factory<flow::Node>& factory,
									const flowview::BoundaryBinders& binders)
{
	fs::create_directories(into);
	Registration registration{{}, into / "rig.json"};
	flowview::RunOptions options;
	options.graphPath = flowviewExample(document);
	const fs::path report = into / "report.txt";
	options.bindings = {"--footage", (capture / "rig").string(), "--models", (capture / models).string(),
						"--report", report.string(), "--rig", registration.rigFile.string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);
	registration.report = readText(report);
	return registration;
}

// `flowview run -g compare-rigs.json --expected <truth> --found <rig> --comparison ...`, which writes the
// comparison's line: the same as the library's from the same two documents, which is what is returned.
static camera::RigComparison scoreRig(const fs::path& truthFile, const fs::path& rigFile, const fs::path& into,
									  const core::Factory<flow::Node>& factory, const flowview::BoundaryBinders& binders)
{
	flowview::RunOptions options;
	options.graphPath = flowviewExample("compare-rigs");
	const fs::path comparisonFile = into / "comparison.txt";
	options.bindings = {"--expected", truthFile.string(), "--found", rigFile.string(), "--comparison", comparisonFile.string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);

	const std::optional<camera::registration::RegisteredRig> truth = readRig(truthFile);
	const std::optional<camera::registration::RegisteredRig> found = readRig(rigFile);
	REQUIRE(truth.has_value());
	REQUIRE(found.has_value());
	const camera::RigComparison comparison = camera::compareRigs(*truth, *found);
	INFO("comparison: " << comparison.toString());
	REQUIRE(comparison.compared());
	CHECK(readText(comparisonFile) == comparison.toString() + "\n");
	// Every camera of the plan registered, and the capture names them as its truth does.
	CHECK(comparison.onlyExpected.empty());
	CHECK(comparison.onlyFound.empty());
	return comparison;
}

TEST_CASE("a synthetic capture registers near-exactly with its true models and worse with its nominal ones",
		  "[rigplan][camera][runmode][gpu]")
{
	lain::testing::skipInDebug("the synthetic-capture vertical runs in Release builds: a Debug build takes minutes to render and detect it");
	registerEverything();
	REQUIRE(camera::board::canRender()); // this test is built only where a board renderer is
	acm::Device& device = render::testing::device();
	const lain::testing::ThreadPool pool(4);

	const fs::path dir = lain::testing::scratchDir() / "capture-vertical";
	fs::remove_all(dir);
	fs::create_directories(dir);

	// 1. The committed plan, rendered as it is stored: intrinsic scale 1 with installation locked to it,
	// the sensor's own noise, its own seed.
	const rigplan::RigPlan plan = rigplan::testing::examplePlan("three-poles");
	rigplan::CaptureOptions options;
	options.out = dir / "capture";
	rigplan::testing::renderCapture(plan, device, options);
	const fs::path capture = options.out;

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	// 2. The same pixels registered twice: with the models the cameras were rendered through, and with the
	// models they were meant to have.
	const Registration withTruth = registerCapture("register", capture, "truth/models", dir / "truth", factory, binders);
	const Registration withNominal = registerCapture("register", capture, "nominal", dir / "nominal", factory, binders);
	INFO("with truth/models: " << withTruth.report);
	INFO("with nominal/: " << withNominal.report);

	if (!camera::refinement::canRefine())
	{
		// The board is detected and every pose solved, and the report says what is missing. A failed
		// registration has no rig, so nothing is written.
		CHECK(withTruth.report.rfind("Failed: NoRefiner", 0) == 0);
		CHECK(withNominal.report.rfind("Failed: NoRefiner", 0) == 0);
		CHECK_FALSE(fs::exists(withTruth.rigFile));
		CHECK_FALSE(fs::exists(withNominal.rigFile));
		return;
	}

	// Every camera sees the board in every frame, so every one of the 24 capture groups is usable.
	CHECK(withTruth.report.rfind("Ready: 6 cameras relative to ", 0) == 0);
	CHECK(withTruth.report.find("24 of 24 groups usable") != std::string::npos);
	// The nominal registration's VERDICT is recorded in WORK.md (M15 slice 7a), not asserted: it was Ready,
	// with the held-out residual of the true one, since a board pose solved through a model that is out
	// absorbs the error. What it costs is the comparison's to show, below.

	// 3. Each scored against the truth. The truth is relative to the rig plan's reference, its first camera
	// (left-low); a registration whose every camera shares every group takes the first by identity
	// (left-high), so the comparison derives the truth again for it.
	const fs::path truthFile = capture / "truth" / "rig.json";
	const camera::RigComparison truthScore = scoreRig(truthFile, withTruth.rigFile, dir / "truth", factory, binders);
	const camera::RigComparison nominalScore = scoreRig(truthFile, withNominal.rigFile, dir / "nominal", factory, binders);
	INFO("scored with truth/models: " << truthScore.toString());
	INFO("scored with nominal/: " << nominalScore.toString());
	CHECK(truthScore.derivedAgain);

	// With the true models, near-exactly: measured 0.05% in scale, 0.68 mrad and 0.40 mm at worst on Metal
	// and 0.67 mrad and 0.39 mm on the CPU device (WORK.md M15 slice 7a). The bounds are M9's rig
	// vertical's, which these are no looser than.
	CHECK(std::abs(truthScore.scale - 1.0) < 0.005);
	CHECK(truthScore.worstRotation()->rotation < 0.002);
	CHECK(truthScore.worstCentre()->centre < 0.001);

	// With the nominal ones, measurably worse: measured 14 times in rotation (9.9 mrad) and 12 times in
	// position (4.8 mm) on both devices. Asserted at 5 times, which nominal models that were the realised
	// ones (each about 1 times) are nowhere near.
	CHECK(nominalScore.worstRotation()->rotation > 5.0 * truthScore.worstRotation()->rotation);
	CHECK(nominalScore.worstCentre()->centre > 5.0 * truthScore.worstCentre()->centre);
}

TEST_CASE("a synthetic capture registers without a target, the same wherever its footage sits",
		  "[rigplan][camera][runmode][gpu]")
{
	lain::testing::skipInDebug("the synthetic-capture vertical runs in Release builds: a Debug build takes minutes to render and extract");
	registerEverything();
	REQUIRE(camera::board::canRender()); // this test is built only where a board renderer is
	acm::Device& device = render::testing::device();
	const lain::testing::ThreadPool pool(4);

	const fs::path dir = lain::testing::scratchDir() / "capture-targetless";
	fs::remove_all(dir);
	fs::create_directories(dir);

	// 1. The committed plan, rendered as it is stored.
	const rigplan::RigPlan plan = rigplan::testing::examplePlan("two-methods");
	rigplan::CaptureOptions options;
	options.out = dir / "capture";
	rigplan::testing::renderCapture(plan, device, options);
	const fs::path capture = options.out;

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	// 2. Registered without a target, with the models the cameras were rendered through.
	const Registration registered = registerCapture("register-targetless", capture, "truth/models", dir / "run", factory, binders);
	INFO("report: " << registered.report);
	if (!camera::refinement::canRefine())
	{
		// The document loads whole and runs, and the report names what is missing before any frame is
		// read. A failed registration has no rig, so nothing is written.
		CHECK(registered.report.rfind("Failed: NoRefiner", 0) == 0);
		CHECK_FALSE(fs::exists(registered.rigFile));
		return;
	}
	// The reference is whichever camera shares the most, and nothing measured the rig, so its scale is
	// arbitrary. Measured, on an RTX 4090 and on the CPU device: relative to cam2, 274 and 269 usable
	// tracks, held out 0.107 and 0.131 mrad (median-based; RMS 0.72 and 0.65).
	CHECK(registered.report.rfind("Ready: 4 cameras relative to cam", 0) == 0);
	CHECK(registered.report.find("arbitrary scale") != std::string::npos);

	// 3. Scored against the truth, the scale fitted. Measured: 1.14 and 0.46 mrad, 2.73 and 1.50 mm, at a
	// scale of 4.22. The bounds are the flowview vertical's this case replaced: it is the path a user
	// takes that is checked here, and test-camera-comparison holds the same registration to the truth
	// more tightly.
	const camera::RigComparison score = scoreRig(capture / "truth" / "rig.json", registered.rigFile, dir / "run", factory, binders);
	INFO("scored: " << score.toString());
	CHECK(score.worstRotation()->rotation < 0.003);
	CHECK(score.worstCentre()->centre < 0.01);

	// 4. The same footage and models somewhere else on disk register the same, byte for byte: a camera
	// is named by its folder within the rig's, never by where the rig sits.
	const fs::path elsewhere = dir / "elsewhere" / "on" / "disk";
	fs::create_directories(elsewhere);
	fs::copy(capture / "rig", elsewhere / "rig", fs::copy_options::recursive);
	fs::copy(capture / "truth" / "models", elsewhere / "models", fs::copy_options::recursive);
	const Registration moved = registerCapture("register-targetless", elsewhere, "models", elsewhere / "run", factory, binders);
	CHECK(moved.report == registered.report);
	CHECK(readText(moved.rigFile) == readText(registered.rigFile));
}
