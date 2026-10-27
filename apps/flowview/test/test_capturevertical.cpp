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
// Built only where a board renderer is, since the plan waves M9's board. Without a refiner the same
// capture registers to a report naming the one it is missing.

#include "camerafiles.h"
#include "clibinders.h"
#include "gpu.h"
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
#include <lain/data/value.h>
#include <lain/flow/node.h>
#include <lain/io/data/load.h>
#include <lain/io/image/codecs.h>
#include <lain/io/mesh/codecs.h>
#include <lain/io/sequence/openers.h>
#include <lain/rigplan/capture.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/testing/scratch.h>
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

// The committed rig plan, read as `rigplan render` reads a document: its paths against its folder,
// its shape strictly.
static rigplan::RigPlan examplePlan(const std::string& name)
{
	const fs::path file = fs::path(LAIN_RIGPLAN_EXAMPLES_DIR) / (name + ".json");
	std::optional<data::Value> document = io::data::load(core::Uri::fromPath(file));
	REQUIRE(document.has_value());
	rigplan::resolvePaths(*document, file.parent_path());
	const rigplan::RigPlanRead read = rigplan::rigPlanFromValue(*document);
	INFO("problems: " << read.problems.size() << (read.problems.empty() ? "" : ", the first " + read.problems.front()));
	REQUIRE(read.plan.has_value());
	return *read.plan;
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

// `flowview run -g register.json --footage <capture>/rig --models <capture>/<models> --report ... --rig ...`.
static Registration registerCapture(const fs::path& capture, const std::string& models, const fs::path& into,
									const core::Factory<flow::Node>& factory, const flowview::BoundaryBinders& binders)
{
	fs::create_directories(into);
	Registration registration{{}, into / "rig.json"};
	flowview::RunOptions options;
	options.graphPath = flowviewExample("register");
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
#ifndef NDEBUG
	SKIP("the synthetic-capture vertical runs in Release builds: a Debug build takes minutes to render and detect it");
#endif
	registerEverything();
	REQUIRE(camera::board::canRender()); // this test is built only where a board renderer is
	acm::Device& device = render::testing::device();
	const lain::testing::ThreadPool pool(4);

	const fs::path dir = lain::testing::scratchDir() / "capture-vertical";
	fs::remove_all(dir);
	fs::create_directories(dir);

	// 1. The committed plan, rendered as it is stored: intrinsic scale 1 with installation locked to it,
	// the sensor's own noise, its own seed.
	const rigplan::RigPlan plan = examplePlan("three-poles");
	rigplan::CaptureOptions options;
	options.out = dir / "capture";
	const rigplan::CaptureReport written = rigplan::writeCapture(plan, device, options);
	INFO("capture problems: " << written.problems.size() << (written.problems.empty() ? "" : ", the first " + written.problems.front()));
	REQUIRE(written.problems.empty());
	CHECK(written.framesWritten == plan.cameras.size() * plan.timeline.frames);
	const fs::path capture = options.out;
	REQUIRE(fs::exists(capture / "capture.json"));

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	// 2. The same pixels registered twice: with the models the cameras were rendered through, and with the
	// models they were meant to have.
	const Registration withTruth = registerCapture(capture, "truth/models", dir / "truth", factory, binders);
	const Registration withNominal = registerCapture(capture, "nominal", dir / "nominal", factory, binders);
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
