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
// In a build without a camera backend the same document is REFUSED: its camera kinds are not
// registered there (ADR-0016), so it does not load as saved.

#include "clibinders.h"
#include "graphio.h"
#include "runmode.h"
#include "scene.h"
#include "syntheticfootage.h" // the footage, drawn in plain C++

#include <lain/camera/backends.h>
#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/calibration/report.h>
#include <lain/camera/cameramodel.h>
#include <lain/camera/flow/register.h>
#include <lain/camera/serialize/cameramodel.h>
#include <lain/core/uri.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/io/data/load.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/math/rigidtransform.h>
#include <lain/media/framesequence.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <variant>

using namespace lain;
namespace fs = std::filesystem;

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

	// footage -> calibrateCamera (board from a boardSpecification of 24 mm squares) -> report, and
	// -> cameraModel -> model. `held` adds an `imported` model input and holds it (HoldAndValidate):
	// the document that checks a model file against footage instead of estimating one.
	fs::path writeDocument(const fs::path& file, bool held = false)
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
		if (held)
		{
			const flow::PortId imported = input.addBoundary<camera::CameraModel>("imported");
			REQUIRE(graph.connect(flow::PortAddress{input.id(), imported}, in(graph, calibrate, 2)) ==
					flow::Connection::Ok);
			setParam(graph, calibrate, "importedPolicy", camera::calibration::ImportedModelPolicy::HoldAndValidate);
		}

		REQUIRE(graph.connect(flow::PortAddress{input.id(), footage}, in(graph, calibrate, 0)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, spec), in(graph, calibrate, 1)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, calibrate), in(graph, extract, 0)) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, calibrate), flow::PortAddress{output.id(), report}) == flow::Connection::Ok);
		REQUIRE(graph.connect(out(graph, extract), flow::PortAddress{output.id(), model}) == flow::Connection::Ok);
		REQUIRE(flowview::saveGraph(core::Uri::fromPath(file), graph, factory));
		return file;
	}

	// The camera the footage is seen through: a 640x480 pinhole with distinct intrinsics.
	const camera::synthetic::Pinhole kCamera{};

	// The footage, as a folder of stills: what a user would bind.
	fs::path writeFootage(const fs::path& dir, std::size_t frames)
	{
		camera::board::PatternParameters parameters;
		parameters.squaresX = 7;
		parameters.squaresY = 5;
		parameters.markerToSquare = 0.75;
		camera::board::Instance instance{"board", {core::Length::fromMillimetres(24), std::nullopt, std::nullopt}};
		const camera::board::Specification spec =
			*camera::board::Specification::create(*camera::board::Pattern::create(parameters).pattern, instance)
				 .specification;
		const std::optional<camera::board::Rendering> rendering =
			camera::board::render(spec.pattern(), camera::board::RenderRequest{60, 20});
		REQUIRE(rendering.has_value());

		const fs::path folder = dir / "footage";
		fs::create_directories(folder);
		for (std::size_t i = 0; i < frames; ++i)
		{
			const std::string name = "frame." + std::string(4 - std::to_string(i).size(), '0') + std::to_string(i) + ".png";
			REQUIRE(io::image::save(core::Uri::fromPath(folder / name),
									camera::synthetic::view(*rendering, spec, kCamera,
															camera::synthetic::sweepPose(spec, i, frames))));
		}
		return folder;
	}

	// Footage with no board in it, for a build that cannot render one: enough frames to be a sequence,
	// which is all a calibration with no detector looks at before it says so.
	fs::path writePlainFootage(const fs::path& dir, std::size_t frames)
	{
		const fs::path folder = dir / "footage";
		fs::create_directories(folder);
		for (std::size_t i = 0; i < frames; ++i)
		{
			const std::string name = "frame." + std::string(4 - std::to_string(i).size(), '0') + std::to_string(i) + ".png";
			REQUIRE(io::image::save(core::Uri::fromPath(folder / name), image::Image(kCamera.width, kCamera.height)));
		}
		return folder;
	}

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
} // namespace

TEST_CASE("run calibrates a camera from a folder of footage", "[camera][runmode]")
{
	ensureRegistered();
	const fs::path dir = lain::testing::scratchDir() / "camera-vertical";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path document = writeDocument(dir / "calibrate.json");

	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	flowview::RunOptions options;
	options.graphPath = document.string();

	const fs::path footage = haveCameraBackend() ? writeFootage(dir, 24) : writePlainFootage(dir, 2);
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
		held.graphPath = writeDocument(dir / "validate.json", true).string();
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
