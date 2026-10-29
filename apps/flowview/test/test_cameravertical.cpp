// Camera calibration end to end through the headless `run` path (runGraph), as a user's build runs it:
// a document naming the camera kinds, a folder of footage bound to its boundary input, and the report
// and model written out as text.
//
// The footage is a board rendered by the production backend and seen by a known pinhole camera along
// a sweep of poses, drawn here in plain C++ (an inverse homography, supersampled) so this test needs
// no OpenCV of its own. What is checked is that the pieces connect: the document saves and loads, the
// sequence binds, the method runs through the real backends and says Ready, the model it found is the
// camera the footage came from, and the model file it wrote binds back into a second run that holds
// it. Accuracy is the plugin's tests' business.
//
// In a build without a camera backend the same document still loads whole, since a camera kind is
// vocabulary (ADR-0016, amended), and runs to a report that names the backend it is missing.
//
// The same holds for a rig: registered by a board, and without a target from the scene its cameras
// share. Its transforms are written out as a rig document, read back by the production reader and
// held to the rig the footage was drawn from, and the file binds back into a later run.

#include "camerafootage.h" // the footage, drawn in plain C++ and shared with flowview-examples
#include "clibinders.h"
#include "graphio.h"
#include "rigcompare.h" // a registered rig against its truth
#include "runmode.h"
#include "scene.h"
#include "syntheticfootage.h" // the footage, drawn in plain C++
#include "testcamera.h"		  // the targetless rig's camera
#include "texturedscene.h"	  // ... and the scene it sees, drawn in plain C++ too

#include <lain/camera/backends.h>
#include <lain/camera/board/detection.h>
#include <lain/camera/board/pose.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/calibration/report.h>
#include <lain/camera/cameramodel.h>
#include <lain/camera/feature/features.h>
#include <lain/camera/feature/geometry.h>
#include <lain/camera/feature/matching.h>
#include <lain/camera/flow/register.h>
#include <lain/camera/refinement/refiner.h>
#include <lain/camera/registration/report.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/camera/serialize/registeredrig.h>
#include <lain/core/uri.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/math/rigidtransform.h>
#include <lain/media/framesequence.h>
#include <lain/task/task.h>
#include <lain/testing/scratch.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
namespace fs = std::filesystem;
namespace cameraFootage = flowview::examples::footage;

namespace
{
	void ensureRegistered()
	{
		static const bool once = []
		{
			io::image::registerImageCodecs();
			io::video::registerVideoCodecs();
			io::sequence::registerSequenceOpeners();
			camera::registerCameraBackends();
			flowview::registerSceneSerialization();
			return true;
		}();
		(void)once;
	}

	// A committed example document (apps/flowview/examples): what the verticals run, as a user would.
	fs::path exampleDocument(const std::string& name)
	{
		return fs::path(LAIN_FLOWVIEW_EXAMPLES_DIR) / (name + ".json");
	}

	bool haveCameraBackend()
	{
		return camera::board::canRender() && camera::board::canDetect() && camera::calibration::canEstimate();
	}

	template <typename T>
	void setParam(flow::Graph& graph, flow::NodeId id, const std::string& name, T value)
	{
		flow::Node& node = graph.node(id);
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() == name)
			{
				REQUIRE(node.setParam(node.param(i).id(), value));
				return;
			}
		}
		FAIL("no parameter named " << name);
	}

	flow::PortAddress in(const flow::Graph& graph, flow::NodeId id, std::size_t index)
	{
		return {id, graph.node(id).input(index).id()};
	}

	flow::PortAddress out(const flow::Graph& graph, flow::NodeId id, std::size_t index = 0)
	{
		return {id, graph.node(id).output(index).id()};
	}

	// calibrate.json's graph, with an `imported` model input held (HoldAndValidate): the document that
	// checks a model file against footage instead of estimating one. Built here: it is this test's
	// round trip, not an example.
	fs::path writeHeldDocument(const fs::path& file)
	{
		// The production palette, which has the camera kinds whatever this build's backends, so the
		// document is written the same in a build that cannot run it.
		core::Factory<flow::Node> factory;
		flowview::registerExampleNodes(factory, 8);
		flow::Graph graph;
		flow::GroupInputNode& input = graph.boundaryInputNode();
		flow::GroupOutputNode& output = graph.boundaryOutputNode();
		const flow::PortId footage = input.addBoundary<media::FrameSequence>("footage");
		const flow::PortId report = output.addBoundary<camera::calibration::Report>("report");
		const flow::PortId model = output.addBoundary<camera::CameraModel>("model");

		const flow::NodeId spec = graph.add(factory.create(camera::kBoardSpecificationKey));
		const flow::NodeId calibrate = graph.add(factory.create(camera::kCalibrateCameraKey));
		const flow::NodeId extract = graph.add(factory.create(camera::kCameraModelKey));
		setParam(graph, spec, "squareLengthMm", 24.0f);
		setParam(graph, calibrate, "resamples", 8);
		// The footage's camera is a pinhole, so that is the model to ask for: asked to fit distortion
		// coefficients to a lens with none, the extra freedom trades against the principal point and
		// the verdict rightly withholds Ready. It is also a param away from its default, so the
		// document has to carry an enum by name for the run to ask for it.
		setParam(graph, calibrate, "model", camera::DistortionModel::None);
		const flow::PortId imported = input.addBoundary<camera::CameraModel>("imported");
		REQUIRE(graph.connect(flow::PortAddress{input.id(), imported}, in(graph, calibrate, 2)) == flow::Connection::Ok);
		setParam(graph, calibrate, "importedPolicy", camera::calibration::ImportedModelPolicy::HoldAndValidate);

		REQUIRE(graph.connect(flow::PortAddress{input.id(), footage}, in(graph, calibrate, 0)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, spec), in(graph, calibrate, 1)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, calibrate), in(graph, extract, 0)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, calibrate), flow::PortAddress{output.id(), report}) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, extract), flow::PortAddress{output.id(), model}) == flow::Connection::Ok);
		REQUIRE(flowview::saveGraph(core::Uri::fromPath(file), graph, factory));
		return file;
	}

	// The camera the board footage is seen through, the footage writers' own (camerafootage.h).
	const camera::synthetic::Pinhole kCamera = cameraFootage::boardCamera();

	std::string readText(const fs::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	// A camera-model document on disk, read back through the production reader.
	std::optional<camera::CameraModel> readModel(const fs::path& file)
	{
		const std::optional<data::Value> document = io::data::load(core::Uri::fromPath(file));
		if (!document)
			return std::nullopt;
		return camera::cameraModelFromValue(*document).model;
	}

	// `entries` of `dir` copied whole into a folder three levels further down, which is returned: the
	// same footage somewhere else on disk.
	fs::path copyElsewhere(const fs::path& dir, const std::vector<std::string>& entries)
	{
		const fs::path elsewhere = dir / "elsewhere" / "on" / "disk";
		fs::remove_all(dir / "elsewhere");
		fs::create_directories(elsewhere);
		for (const std::string& entry : entries)
			fs::copy(dir / entry, elsewhere / entry, fs::copy_options::recursive);
		return elsewhere;
	}
} // namespace

TEST_CASE("run calibrates a camera from a folder of footage", "[camera][runmode]")
{
	ensureRegistered();
	const fs::path dir = lain::testing::scratchDir() / "camera-vertical";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path document = exampleDocument("calibrate");

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	flowview::RunOptions options;
	options.graphPath = document.string();

	const fs::path footage = cameraFootage::writeBoardFootage(dir, haveCameraBackend() ? 24 : 2, haveCameraBackend());
	const fs::path reportText = dir / "report.txt";
	const fs::path modelFile = dir / "model.json";
	options.bindings = {"--footage", footage.string(), "--report", reportText.string(), "--model", modelFile.string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);

	if (!haveCameraBackend())
	{
		// No backend: the document still loads whole and runs (a camera kind is vocabulary, ADR-0016
		// amended), and the report says what is missing instead of describing a camera. No model.
		const std::string report = readText(reportText);
		INFO("report: " << report);
		CHECK(report.rfind("Failed: NoDetector", 0) == 0);
		CHECK_FALSE(fs::exists(modelFile));
		return;
	}

	const std::string report = readText(reportText);
	INFO("report: " << report);
	CHECK(report.rfind("Ready: ", 0) == 0);

	// The model output is a camera-model DOCUMENT, read back by the production reader.
	const std::optional<camera::CameraModel> model = readModel(modelFile);
	REQUIRE(model.has_value());
	INFO("model: " << model->toString());
	CHECK(std::holds_alternative<camera::NoDistortion>(model->distortion()));
	CHECK(model->image() == camera::ImageGeometry{std::uint32_t(kCamera.width), std::uint32_t(kCamera.height)});
	// Measured 0.14% and 0.3 px off; the bounds leave room for another platform's arithmetic, and
	// still catch a model that is not this camera's.
	CHECK(std::abs(model->intrinsics().fx - kCamera.fx) < 0.005 * kCamera.fx);
	CHECK(std::abs(model->intrinsics().fy - kCamera.fy) < 0.005 * kCamera.fy);
	CHECK(std::abs(model->intrinsics().cx - kCamera.cx) < 1.0);
	CHECK(std::abs(model->intrinsics().cy - kCamera.cy) < 1.0);

	SECTION("the model file binds back, and a held model is validated rather than re-estimated")
	{
		// The round trip a user makes: calibrate once, keep model.json, then check it against other
		// footage. `--imported` reads the file the first run wrote.
		flowview::RunOptions held;
		held.graphPath = writeHeldDocument(dir / "validate.json").string();
		const fs::path heldReport = dir / "held.txt";
		const fs::path heldModel = dir / "held.json";
		held.bindings = {"--footage", footage.string(), "--imported", modelFile.string(),
						 "--report", heldReport.string(), "--model", heldModel.string()};
		REQUIRE(flowview::runGraph(held, factory, binders) == 0);

		// A held model can demonstrate no stability of its own, so it is at most Exploratory.
		const std::string text = readText(heldReport);
		INFO("held report: " << text);
		CHECK(text.rfind("Exploratory: ", 0) == 0);
		// ... and it comes out exactly as it went in: held, never estimated.
		const std::optional<camera::CameraModel> out = readModel(heldModel);
		REQUIRE(out.has_value());
		CHECK(camera::cameraModelToValue(*out) == camera::cameraModelToValue(*model));
	}
}

namespace
{
	// rig -> copy: a rig bound from a file and written straight back out, which is all a run can do
	// with one until a node consumes a rig.
	fs::path writeRigCopyDocument(const fs::path& file)
	{
		core::Factory<flow::Node> factory;
		flowview::registerExampleNodes(factory, 8);
		flow::Graph graph;
		const flow::PortId rig = graph.boundaryInputNode().addBoundary<camera::registration::RegisteredRig>("rig");
		const flow::PortId copy = graph.boundaryOutputNode().addBoundary<camera::registration::RegisteredRig>("copy");
		REQUIRE(graph.connect(flow::PortAddress{graph.boundaryInputNode().id(), rig},
							  flow::PortAddress{graph.boundaryOutputNode().id(), copy}) == flow::Connection::Ok);
		REQUIRE(flowview::saveGraph(core::Uri::fromPath(file), graph, factory));
		return file;
	}

	bool haveRegistrationBackends()
	{
		return camera::board::canRender() && camera::board::canDetect() && camera::board::canSolvePose() &&
			   camera::refinement::canRefine();
	}

} // namespace

// A rig document on disk, read back through the production reader.
static std::optional<camera::registration::RegisteredRig> readRig(const fs::path& file)
{
	const std::optional<data::Value> document = io::data::load(core::Uri::fromPath(file));
	if (!document)
		return std::nullopt;
	const camera::registration::RegisteredRigRead read = camera::registration::registeredRigFromValue(*document);
	INFO("problems: " << read.problems.size() << (read.problems.empty() ? "" : ", the first " + read.problems.front()));
	CHECK(read.problems.empty());
	return read.rig;
}

// How far a written rig is from `worldFromCamera`, the poses its cam0..cam2 footage was drawn from,
// once the truth is made relative to the rig's own reference. Each camera is named by its footage
// folder within the rig's folder, so the rig lists them in that order.
static camera::testing::RigDifference rigAgainstTruth(const camera::registration::RegisteredRig& rig,
													  const std::vector<math::RigidTransformd>& worldFromCamera)
{
	REQUIRE(rig.cameras.size() == worldFromCamera.size());
	std::optional<std::size_t> reference;
	std::vector<math::RigidTransformd> found;
	for (std::size_t c = 0; c < rig.cameras.size(); ++c)
	{
		CHECK(rig.cameras[c].camera.value == "cam" + std::to_string(c));
		if (rig.cameras[c].camera == rig.reference)
			reference = c;
		found.push_back(rig.cameras[c].referenceFromCamera);
	}
	REQUIRE(reference.has_value());
	return camera::testing::compare(camera::testing::rebased(worldFromCamera, *reference), found);
}

TEST_CASE("run registers a rig from a folder of footage and a folder of models", "[camera][runmode]")
{
	ensureRegistered();
	const fs::path dir = lain::testing::scratchDir() / "rig-vertical";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path document = exampleDocument("register");
	cameraFootage::writeBoardRig(dir, haveRegistrationBackends() ? 16 : 2, camera::board::canRender());

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	flowview::RunOptions options;
	options.graphPath = document.string();
	const fs::path reportText = dir / "report.txt";
	const fs::path rigFile = dir / "rig.json";
	options.bindings = {"--footage", (dir / "rig").string(), "--models", (dir / "models").string(), "--report",
						reportText.string(), "--rig", rigFile.string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);

	const std::string report = readText(reportText);
	INFO("report: " << report);
	if (!camera::board::canDetect())
	{
		// The document loads whole and runs without a backend, and the report says what is missing.
		// No rig: a failed registration has none, so nothing is written.
		CHECK(report.rfind("Failed: NoDetector", 0) == 0);
		CHECK_FALSE(fs::exists(rigFile));
		return;
	}
	if (!haveRegistrationBackends())
	{
		CHECK(report.rfind("Failed: NoRefiner", 0) == 0);
		CHECK_FALSE(fs::exists(rigFile));
		return;
	}
	// Each camera is named by its footage folder within the rig's folder, cam0 first since the folder
	// lists by name, and the reference is cam0, every camera sharing every group.
	CHECK(report.rfind("Ready: 3 cameras relative to cam0, metric", 0) == 0);
	CHECK(report.find("16 of 16 groups usable") != std::string::npos);

	// The rig output is a rig DOCUMENT holding every camera's transform, which the report's line does
	// not, in metres since a board measured the rig.
	const std::optional<camera::registration::RegisteredRig> rig = readRig(rigFile);
	REQUIRE(rig.has_value());
	INFO("rig: " << rig->toString());
	CHECK(rig->scale.scale == camera::registration::Scale::Metric);
	CHECK(report.find("relative to " + rig->reference.value) != std::string::npos);
	const camera::testing::RigDifference difference =
		rigAgainstTruth(*rig, {cameraFootage::boardRigCamera(0), cameraFootage::boardRigCamera(1), cameraFootage::boardRigCamera(2)});
	INFO("scale " << difference.scale << ", worst rotation " << difference.worstRotation * 1000.0
				  << " mrad, worst centre " << difference.worstCentre * 1000.0 << " mm");
	// Measured 0.05% in scale, 0.29 mrad and 0.08 mm out. The bounds leave room for another platform's
	// arithmetic, and still catch a transform written the wrong way round (cameraFromReference turns
	// each camera 0.25 rad the wrong way), or a camera given another's model: each has its own pinhole
	// (camerafootage.h), and pairing them one off fails four of these checks.
	CHECK(std::abs(difference.scale - 1.0) < 0.005);
	CHECK(difference.worstRotation < 0.002);
	CHECK(difference.worstCentre < 0.001);

	// The same footage somewhere else on disk registers the same, byte for byte: each camera is named,
	// and each group's frames are spelled, relative to the folder the rig lies in (capture/location.h),
	// so the same groups are held out and resampled. Before 2026-10-21 a camera was named by its
	// footage's absolute path, and another folder held out other evidence.
	const fs::path elsewhere = copyElsewhere(dir, {"rig", "models"});
	options.bindings = {"--footage", (elsewhere / "rig").string(), "--models", (elsewhere / "models").string(),
						"--report", (elsewhere / "report.txt").string(), "--rig", (elsewhere / "rig.json").string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);
	CHECK(readText(elsewhere / "report.txt") == report);
	CHECK(readText(elsewhere / "rig.json") == readText(rigFile));

	SECTION("the rig file binds back")
	{
		flowview::RunOptions copy;
		copy.graphPath = writeRigCopyDocument(dir / "copy.json").string();
		const fs::path copied = dir / "copied.json";
		copy.bindings = {"--rig", rigFile.string(), "--copy", copied.string()};
		REQUIRE(flowview::runGraph(copy, factory, binders) == 0);
		const std::optional<camera::registration::RegisteredRig> back = readRig(copied);
		REQUIRE(back.has_value());
		CHECK(back->reference == rig->reference);
		CHECK(back->scale.evidence == rig->scale.evidence);
		REQUIRE(back->cameras.size() == rig->cameras.size());
		for (std::size_t c = 0; c < rig->cameras.size(); ++c)
		{
			CHECK(back->cameras[c].camera == rig->cameras[c].camera);
			CHECK(back->cameras[c].referenceFromCamera.translation() == rig->cameras[c].referenceFromCamera.translation());
			// To the last bit a renormalised rotation can move (RigidTransform normalises what it is built from).
			CHECK(camera::testing::rotationBetween(back->cameras[c].referenceFromCamera, rig->cameras[c].referenceFromCamera) <=
				  1e-15);
		}
	}
}

namespace
{
	bool haveTargetlessBackends()
	{
		return camera::feature::canExtract() && camera::feature::canMatch() && camera::feature::canSolveGeometry() &&
			   camera::refinement::canRefine();
	}

} // namespace

TEST_CASE("run registers a rig without a target from a folder of footage and a folder of models", "[camera][runmode]")
{
	ensureRegistered();
	const fs::path dir = lain::testing::scratchDir() / "targetless-vertical";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path document = exampleDocument("register-targetless");
	cameraFootage::writeTexturedRig(dir, haveTargetlessBackends());

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	flowview::RunOptions options;
	options.graphPath = document.string();
	const fs::path reportText = dir / "report.txt";
	const fs::path rigFile = dir / "rig.json";
	options.bindings = {"--footage", (dir / "rig").string(), "--models", (dir / "models").string(), "--report",
						reportText.string(), "--rig", rigFile.string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);

	const std::string report = readText(reportText);
	INFO("report: " << report);
	if (!camera::feature::canExtract())
	{
		// The document loads whole and runs without a backend, and the report says what is missing.
		CHECK(report.rfind("Failed: NoFeatureExtractor", 0) == 0);
		CHECK_FALSE(fs::exists(rigFile));
		return;
	}
	if (!haveTargetlessBackends())
	{
		CHECK(report.rfind("Failed: NoRefiner", 0) == 0);
		CHECK_FALSE(fs::exists(rigFile));
		return;
	}
	// Each camera is named by its footage folder within the rig's folder; the reference is whichever
	// shares the most, and nothing measured the rig, so its scale is arbitrary.
	//
	// Measured: 432 usable tracks, 346 landmarks, 86 held out, relative to cam1; 12 s in Debug. Until
	// 2026-10-21 a camera's name was its folder's absolute path, which held this run's scratch
	// directory: a track's identity digests its cameras' names, and every k-th track in identity order
	// is held out, so each run held out other tracks (over 20 runs 0.68 to 1.26 mrad held out, Ready
	// every time). Now the same footage gives the same report wherever it is, which the end of this
	// case checks.
	CHECK(report.rfind("Ready: 3 cameras relative to cam", 0) == 0);
	CHECK(report.find("arbitrary scale") != std::string::npos);
	CHECK(report.find("landmarks from") != std::string::npos);

	// The rig document holds every camera's transform, at the arbitrary scale the report names, so its
	// centres are held to the truth after a scale fit.
	const std::optional<camera::registration::RegisteredRig> rig = readRig(rigFile);
	REQUIRE(rig.has_value());
	INFO("rig: " << rig->toString());
	CHECK(rig->scale.scale == camera::registration::Scale::Arbitrary);
	CHECK(report.find("relative to " + rig->reference.value) != std::string::npos);
	const camera::testing::RigDifference difference =
		rigAgainstTruth(*rig, {cameraFootage::texturedRigCamera(0), cameraFootage::texturedRigCamera(1), cameraFootage::texturedRigCamera(2)});
	INFO("scale " << difference.scale << ", worst rotation " << difference.worstRotation * 1000.0
				  << " mrad, worst centre " << difference.worstCentre * 1000.0 << " mm");
	// Measured over five runs, which held out different tracks before 2026-10-21 (above): 0.18 to 0.51
	// mrad, and 0.7 to 1.7 mm on cameras 3.2 m from the scene.
	CHECK(difference.worstRotation < 0.003);
	CHECK(difference.worstCentre < 0.01);

	// The same footage somewhere else on disk registers the same, byte for byte (capture/location.h).
	const fs::path elsewhere = copyElsewhere(dir, {"rig", "models"});
	options.bindings = {"--footage", (elsewhere / "rig").string(), "--models", (elsewhere / "models").string(),
						"--report", (elsewhere / "report.txt").string(), "--rig", (elsewhere / "rig.json").string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);
	CHECK(readText(elsewhere / "report.txt") == report);
	CHECK(readText(elsewhere / "rig.json") == readText(rigFile));
}

TEST_CASE("a targetless registration's matching travels in its document by name", "[camera][runmode]")
{
	// The vertical runs Exact, the default, since Approximate's tracks vary from run to run. So the
	// enum a document must carry by name is checked here: register-targetless.json set to Approximate,
	// saved, loaded through the production path, and Approximate again rather than the default.
	ensureRegistered();
	const fs::path dir = lain::testing::scratchDir() / "targetless-document";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path document = dir / "approximate.json";

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	{
		flow::serialize::LoadResult example = flowview::loadGraph(core::Uri::fromPath(exampleDocument("register-targetless")), factory, nullptr);
		REQUIRE(example.clean());
		std::size_t set = 0;
		for (const flow::NodeId id : example.graph.nodeIds())
		{
			flow::Node& node = example.graph.node(id);
			for (std::size_t i = 0; i < node.paramCount(); ++i)
			{
				if (node.param(i).name() == "matchSearch")
				{
					REQUIRE(node.setParam(node.param(i).id(), camera::feature::MatchSearch::Approximate));
					++set;
				}
			}
		}
		REQUIRE(set == 1);
		REQUIRE(flowview::saveGraph(core::Uri::fromPath(document), example.graph, factory));
	}
	flow::serialize::LoadResult loaded = flowview::loadGraph(core::Uri::fromPath(document), factory, nullptr);
	INFO(loaded.issues.size() << " load issues");
	REQUIRE(loaded.clean());
	std::size_t found = 0;
	for (const flow::NodeId id : loaded.graph.nodeIds())
	{
		const flow::Node& node = loaded.graph.node(id);
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() != "matchSearch")
				continue;
			++found;
			REQUIRE(node.param(i).holds<camera::feature::MatchSearch>());
			CHECK(node.param(i).get<camera::feature::MatchSearch>() == camera::feature::MatchSearch::Approximate);
		}
	}
	CHECK(found == 1);
}

TEST_CASE("run calibrates every camera of a rig and registers it with those models", "[camera][runmode]")
{
	// The committed calibrate-and-register.json: the rig's footage alone is bound, each camera is
	// calibrated through a map, and the models it gathers register the rig by the same board. Nothing
	// tells it the cameras' models, so a rig that matches its truth is one the calibrations got right.
	ensureRegistered();
	const fs::path dir = lain::testing::scratchDir() / "calibrate-and-register";
	fs::remove_all(dir);
	fs::create_directories(dir);
	// Full footage wherever the cameras can be calibrated, a refiner or not: without one the
	// calibrations still run, and only the registration says what it lacks.
	cameraFootage::writeBoardRig(dir, camera::board::canDetect() ? 24 : 2, camera::board::canRender());

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	flowview::RunOptions options;
	options.graphPath = exampleDocument("calibrate-and-register").string();
	const fs::path reportText = dir / "report.txt";
	const fs::path rigFile = dir / "rig.json";
	options.bindings = {"--footage", (dir / "rig").string(), "--report", reportText.string(), "--rig", rigFile.string()};
	REQUIRE(flowview::runGraph(options, factory, binders) == 0);

	if (!camera::board::canDetect())
	{
		// No detector: every calibration fails, each element leaves a hole, the gathered models are
		// cleared (ADR-0014), and the registration is suppressed, so nothing is written.
		CHECK_FALSE(fs::exists(reportText));
		CHECK_FALSE(fs::exists(rigFile));
		return;
	}
	const std::string report = readText(reportText);
	INFO("report: " << report);
	if (!haveRegistrationBackends())
	{
		// The calibrations need no refiner, so they run and are gathered; the registration then says
		// what it lacks. (Found by the no-refiner build on 2026-10-21: drawn with 2 frames a camera, as
		// it first was, every calibration failed and nothing was written.)
		CHECK(report.rfind("Failed: NoRefiner", 0) == 0);
		CHECK_FALSE(fs::exists(rigFile));
		return;
	}
	CHECK(report.rfind("Ready: 3 cameras relative to cam0, metric", 0) == 0);
	CHECK(report.find("24 of 24 groups usable") != std::string::npos);

	const std::optional<camera::registration::RegisteredRig> rig = readRig(rigFile);
	REQUIRE(rig.has_value());
	INFO("rig: " << rig->toString());
	const camera::testing::RigDifference difference =
		rigAgainstTruth(*rig, {cameraFootage::boardRigCamera(0), cameraFootage::boardRigCamera(1), cameraFootage::boardRigCamera(2)});
	INFO("scale " << difference.scale << ", worst rotation " << difference.worstRotation * 1000.0
				  << " mrad, worst centre " << difference.worstCentre * 1000.0 << " mm");
	// Held to the bounds the register vertical holds a rig with its true models to. Measured: 0.05% in
	// scale, 0.87 mrad and 0.24 mm out, in 2 s in Release. At 16 frames a camera, as the register
	// vertical draws, the calibrations were looser (0.44%, 1.3 mrad and 0.87 mm with one shared
	// pinhole), too near the bounds. Each camera has its own pinhole (camerafootage.h), so a camera
	// registered with another's model fails here: pairing them one off fails four of these checks.
	CHECK(std::abs(difference.scale - 1.0) < 0.005);
	CHECK(difference.worstRotation < 0.002);
	CHECK(difference.worstCentre < 0.001);
}

// The calibration fallback end to end: the committed calibrate-fallback.json (apps/flowview/examples),
// run through runGraph as a user runs it, with --footage and --method bound and the attempts and the
// model written out. Which attempts were made is read from the attempts text: board calibration first,
// and targetless calibration only when the board's attempt is not accepted.

namespace
{
	const fs::path kFallbackDocument = exampleDocument("calibrate-fallback");

	struct FallbackRun
	{
		int status = 0;
		std::optional<std::string> attempts; // the attempts text, when it was written
		bool wroteModel = false;
		std::optional<camera::CameraModel> model;
	};

	// One run of the example document. `method` unset leaves --method unbound.
	FallbackRun runFallback(const fs::path& dir, const fs::path& footage, const std::optional<std::string>& method)
	{
		core::Factory<flow::Node> factory;
		flowview::registerExampleNodes(factory, 8);
		flowview::BoundaryBinders binders;
		flowview::registerBoundaryBinders(binders);
		flowview::RunOptions options;
		options.graphPath = kFallbackDocument.string();
		const fs::path attempts = dir / "attempts.txt";
		const fs::path model = dir / "model.json";
		fs::remove(attempts);
		fs::remove(model);
		options.bindings = {"--footage", footage.string(), "--attempts", attempts.string(), "--model", model.string()};
		if (method)
		{
			options.bindings.push_back("--method");
			options.bindings.push_back(*method);
		}

		FallbackRun run;
		run.status = flowview::runGraph(options, factory, binders);
		if (fs::exists(attempts))
			run.attempts = readText(attempts);
		run.wroteModel = fs::exists(model);
		if (run.wroteModel)
			run.model = readModel(model);
		return run;
	}

	fs::path fallbackDir(const std::string& name)
	{
		const fs::path dir = lain::testing::scratchDir() / ("fallback-" + name);
		fs::remove_all(dir);
		fs::create_directories(dir);
		return dir;
	}

	bool startsWith(const std::string& text, const std::string& prefix)
	{
		return text.rfind(prefix, 0) == 0;
	}

} // namespace

TEST_CASE("the calibration fallback accepts the board's attempt and never makes the targetless one", "[camera][runmode][fallback]")
{
	ensureRegistered();
	const fs::path dir = fallbackDir("board");
	const fs::path footage = cameraFootage::writeBoardFootage(dir, haveCameraBackend() ? 24 : 2, haveCameraBackend());
	const FallbackRun run = runFallback(dir, footage, std::string{"board-then-targetless"});
	REQUIRE(run.status == 0);
	REQUIRE(run.attempts.has_value());
	INFO("attempts: " << *run.attempts);

	if (!haveCameraBackend())
	{
		// No backend: the document loads whole and runs, both attempts are made, and each says what
		// this build is missing. Nothing is accepted, so no model is written.
		CHECK(startsWith(*run.attempts, "No attempt of 2 accepted; 1: Failed: NoDetector"));
		CHECK(run.attempts->find("; 2: Failed: NoFeatureExtractor") != std::string::npos);
		CHECK_FALSE(run.wroteModel);
		return;
	}

	// One attempt, accepted: the targetless attempt was gated off, so the attempts hold the board's
	// alone. A targetless attempt made anyway would read "Attempt 1 of 2".
	CHECK(startsWith(*run.attempts, "Attempt 1 of 1 accepted: Ready: "));
	REQUIRE(run.model.has_value());
	INFO("model: " << run.model->toString());
	// Measured 0.14% and 0.3 px off, as the board vertical above measures; the same bounds.
	CHECK(std::holds_alternative<camera::NoDistortion>(run.model->distortion()));
	CHECK(std::abs(run.model->intrinsics().fx - kCamera.fx) < 0.005 * kCamera.fx);
	CHECK(std::abs(run.model->intrinsics().fy - kCamera.fy) < 0.005 * kCamera.fy);
	CHECK(std::abs(run.model->intrinsics().cx - kCamera.cx) < 1.0);
	CHECK(std::abs(run.model->intrinsics().cy - kCamera.cy) < 1.0);

	// The same footage somewhere else on disk calibrates the same, byte for byte: board calibration
	// orders its views by their sources, which keep their order wherever the folder is.
	const fs::path elsewhere = copyElsewhere(dir, {footage.filename().string()});
	const FallbackRun moved = runFallback(elsewhere, elsewhere / footage.filename(), std::string{"board-then-targetless"});
	REQUIRE(moved.status == 0);
	CHECK(moved.attempts == run.attempts);
	CHECK(readText(elsewhere / "model.json") == readText(dir / "model.json"));
}

TEST_CASE("the calibration fallback falls back to targetless calibration when the board's attempt fails", "[camera][runmode][fallback]")
{
	ensureRegistered();
	if (!camera::feature::canExtract())
		SKIP("a build with no camera backend: the board case runs both attempts to their missing backends");
	const fs::path dir = fallbackDir("targetless");

	if (!camera::refinement::canRefine())
	{
		// OpenCV without Ceres. Board calibration needs no refiner, so it runs and fails for footage
		// with no board in it; only then is the targetless attempt made, and it says what it lacks.
		const FallbackRun run = runFallback(dir, cameraFootage::writeBoardFootage(dir, 2, false), std::string{"board-then-targetless"});
		REQUIRE(run.status == 0);
		REQUIRE(run.attempts.has_value());
		INFO("attempts: " << *run.attempts);
		CHECK(startsWith(*run.attempts, "No attempt of 2 accepted; 1: Failed: "));
		CHECK(run.attempts->find("; 2: Failed: NoRefiner") != std::string::npos);
		CHECK_FALSE(run.wroteModel);
		return;
	}

#ifndef NDEBUG
	SKIP("the targetless fallback runs in Release builds: a Debug build takes seconds to draw each frame");
#endif
	const lain::testing::ThreadPool pool;
	const fs::path footage = cameraFootage::writeMovingFootage(dir, 40);
	const FallbackRun run = runFallback(dir, footage, std::string{"board-then-targetless"});
	REQUIRE(run.status == 0);
	REQUIRE(run.attempts.has_value());
	INFO("attempts: " << *run.attempts);

	// The board's attempt failed (there is no board), so the targetless one was made, and accepted.
	CHECK(startsWith(*run.attempts, "Attempt 2 of 2 accepted: Ready: "));
	REQUIRE(run.model.has_value());
	INFO("model: " << run.model->toString());
	// Measured on Linux: 32 frames placed, held out 0.737 px RMS, the model 0.18% out in focal length
	// and 0.7 px in its principal point; 20 s in Release with four workers. The bounds leave room for
	// another platform's features, since macOS's and Windows' SIFT find others (after sub-slice 10e),
	// and still catch a model that is not this camera's.
	CHECK(std::holds_alternative<camera::NoDistortion>(run.model->distortion()));
	CHECK(std::abs(run.model->intrinsics().fx - kCamera.fx) < 0.01 * kCamera.fx);
	CHECK(std::abs(run.model->intrinsics().fy - kCamera.fy) < 0.01 * kCamera.fy);
	CHECK(std::abs(run.model->intrinsics().cx - kCamera.cx) < 3.0);
	CHECK(std::abs(run.model->intrinsics().cy - kCamera.cy) < 3.0);

	// The same footage somewhere else on disk calibrates the same, byte for byte: the camera is named,
	// and each track's frames are spelled, relative to the folder its footage lies in
	// (capture/location.h), so the same tracks are resampled.
	const fs::path elsewhere = copyElsewhere(dir, {footage.filename().string()});
	const FallbackRun moved = runFallback(elsewhere, elsewhere / footage.filename(), std::string{"board-then-targetless"});
	REQUIRE(moved.status == 0);
	CHECK(moved.attempts == run.attempts);
	CHECK(readText(elsewhere / "model.json") == readText(dir / "model.json"));
}

TEST_CASE("the calibration fallback makes only the attempts its method includes", "[camera][runmode][fallback]")
{
	// --method board, on footage the board cannot be found in: one attempt, failed, and no fallback,
	// since the method did not include one. Blank footage, so this runs in every build.
	ensureRegistered();
	const fs::path dir = fallbackDir("board-only");
	const FallbackRun run = runFallback(dir, cameraFootage::writeBoardFootage(dir, 2, false), std::string{"board"});
	REQUIRE(run.status == 0);
	REQUIRE(run.attempts.has_value());
	INFO("attempts: " << *run.attempts);
	// With a backend the board's attempt is TooFewViews (no usable view of 2 frames); without one,
	// NoDetector. Either way it is the only attempt.
	CHECK(startsWith(*run.attempts, "No attempt of 1 accepted; 1: Failed: "));
	CHECK(run.attempts->find("; 2: ") == std::string::npos);
	CHECK_FALSE(run.wroteModel);
}

TEST_CASE("with no --method the calibration fallback attempts nothing", "[camera][runmode][fallback]")
{
	// The cost of having no graph input defaults (WORK.md, M9 slice 4, sub-slice 12 planned): an
	// unbound method is empty, calibrationMethod is suppressed, both gates stay shut and nothing is
	// attempted. The run warns that --method was not bound and still exits 0, as any unbound input
	// does, and writes neither output.
	ensureRegistered();
	const fs::path dir = fallbackDir("unbound");
	const FallbackRun run = runFallback(dir, cameraFootage::writeBoardFootage(dir, 2, false), std::nullopt);
	CHECK(run.status == 0);
	CHECK_FALSE(run.attempts.has_value());
	CHECK_FALSE(run.wroteModel);
}

TEST_CASE("a mistyped --method is refused", "[camera][runmode][fallback]")
{
	// Refused at the binding, which fails the run: an unparsable method is not left to look like an
	// unbound one. The error names the spellings that would have bound.
	ensureRegistered();
	const fs::path dir = fallbackDir("mistyped");
	const FallbackRun run = runFallback(dir, cameraFootage::writeBoardFootage(dir, 2, false), std::string{"board-then"});
	CHECK(run.status != 0);
	CHECK_FALSE(run.attempts.has_value());
	CHECK_FALSE(run.wroteModel);
}
