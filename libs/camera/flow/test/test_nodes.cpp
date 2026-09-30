// The camera node kinds through a real graph and the production scheduler, over stand-in backends:
// the method module's synthetic scene (a detector and an estimator answering from a known truth) and
// a renderer that draws a flat raster. What is checked is the NODES — that each hands the method what
// its parameters say and passes on what comes back — not detection or estimation, which the method
// module's and the OpenCV plugin's own tests cover.

#include "syntheticboard.h"

#include <lain/camera/flow/boardspecificationnode.h>
#include <lain/camera/flow/calibratecameranode.h>
#include <lain/camera/flow/cameramodelnode.h>
#include <lain/camera/flow/detectboardnode.h>
#include <lain/camera/flow/renderboardnode.h>
#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <variant>

using namespace lain;
using namespace lain::camera::testing;
using camera::CameraModel;
using camera::calibration::Report;
namespace board = lain::camera::board;

namespace
{
	// A flat white raster of exactly the size asked for, which is the whole of a renderer's contract.
	class FlatRenderer : public board::Renderer
	{
	public:
		image::Image raster(const board::Pattern& pattern, const board::RenderRequest& request) const override
		{
			const board::PatternParameters& p = pattern.parameters();
			const int w = int(p.squaresX * request.pixelsPerSquare + 2 * request.marginPixels);
			const int h = int(p.squaresY * request.pixelsPerSquare + 2 * request.marginPixels);
			image::Image out{w, h, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
			std::fill(out.data(), out.data() + std::size_t(w) * std::size_t(h), std::uint8_t{255});
			return out;
		}
	};

	void reset()
	{
		resetScene();
		static const bool once = []
		{
			board::rendererRegistry().registerType<FlatRenderer>("flat");
			return true;
		}();
		(void)once;
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

	flow::PortAddress output(const flow::Graph& graph, flow::NodeId id, std::size_t index = 0)
	{
		return {id, graph.node(id).output(index).id()};
	}

	// A board specification node configured as the synthetic scene's board: 7x5, 24 mm squares.
	flow::NodeId sceneBoard(flow::Graph& graph)
	{
		const flow::NodeId id = graph.add<camera::BoardSpecificationNode>();
		setParam(graph, id, "squareLengthMm", 24.0f);
		return id;
	}

	// A boundary input of type T feeding input `to` of `node`, bound to `value` once there is an
	// evaluation; returns the pin.
	template <typename T>
	flow::PortAddress feed(flow::Graph& graph, const std::string& name, flow::NodeId node, std::size_t to)
	{
		flow::GroupInputNode& in = graph.boundaryInputNode();
		const flow::PortId pin = in.addBoundary<T>(name);
		REQUIRE(graph.connect(flow::PortAddress{in.id(), pin}, flow::PortAddress{node, graph.node(node).input(to).id()}) ==
				flow::Connection::Ok);
		return {in.id(), pin};
	}

	template <typename T>
	void bind(flow::Evaluation& evaluation, flow::PortAddress pin, T value)
	{
		flow::PortValue v;
		v.set<T>(std::move(value));
		evaluation.bind(pin, std::move(v));
	}

	// boardSpecification -> calibrateCamera over the scene's footage, with `configure` applied to the
	// calibrate node; the report, run.
	template <typename Configure>
	Report calibrateScene(Configure configure)
	{
		flow::Graph graph;
		const flow::NodeId spec = sceneBoard(graph);
		const flow::NodeId calibrate = graph.add<camera::CalibrateCameraNode>();
		REQUIRE(graph.connect(output(graph, spec), flow::PortAddress{calibrate, graph.node(calibrate).input(1).id()}) ==
				flow::Connection::Ok);
		const flow::PortAddress footagePin = feed<media::FrameSequence>(graph, "footage", calibrate, 0);
		configure(graph, calibrate);

		flow::Evaluation evaluation{graph};
		bind(evaluation, footagePin, footage(scene().frames));
		flow::SerialScheduler{}.run(graph, evaluation);
		const flow::PortValue& report = evaluation.value(output(graph, calibrate));
		REQUIRE(report.holds<Report>());
		return report.get<Report>();
	}
} // namespace

TEST_CASE("a board specification is built from its parameters", "[camera][flow]")
{
	reset();
	flow::Graph graph;
	const flow::NodeId id = graph.add<camera::BoardSpecificationNode>();

	SECTION("the defaults are a common 7x5 board of 30 mm squares")
	{
		flow::Evaluation evaluation{graph};
		flow::SerialScheduler{}.run(graph, evaluation);
		const board::Specification& spec = evaluation.value(output(graph, id)).get<board::Specification>();
		const board::PatternParameters& p = spec.pattern().parameters();
		CHECK(p.squaresX == 7);
		CHECK(p.squaresY == 5);
		CHECK(p.markerToSquare == 0.75);
		CHECK(p.dictionary == board::Dictionary::Aruco5x5_100);
		CHECK(p.layout == board::CharucoLayout::Standard);
		CHECK(spec.instance().identity == "board");
		CHECK(spec.instance().squareLength.value == core::Length::fromMillimetres(30));
		// The fingerprint is the pattern's own: the node adds nothing to what identifies a board.
		CHECK(spec.pattern().fingerprint() == board::Pattern::create(p).pattern->fingerprint());
	}

	SECTION("a float parameter means the decimal that was typed")
	{
		// 0.7f is 0.699999988 and 23.7f is 23.700000763: taken as they are stored, the board would be
		// a slightly different board from the one the user typed in.
		setParam(graph, id, "markerToSquare", 0.7f);
		setParam(graph, id, "squareLengthMm", 23.7f);
		setParam(graph, id, "dictionary", board::Dictionary::Aruco6x6_250);
		setParam(graph, id, "firstMarkerId", 17);
		setParam(graph, id, "layout", board::CharucoLayout::Legacy);
		flow::Evaluation evaluation{graph};
		flow::SerialScheduler{}.run(graph, evaluation);
		const board::Specification& spec = evaluation.value(output(graph, id)).get<board::Specification>();
		CHECK(spec.pattern().parameters().markerToSquare == 0.7);
		CHECK(spec.instance().squareLength.value == core::Length::fromMillimetres(23.7));
		CHECK(spec.pattern().parameters().dictionary == board::Dictionary::Aruco6x6_250);
		CHECK(spec.pattern().parameters().firstMarkerId == 17);
		CHECK(spec.pattern().parameters().layout == board::CharucoLayout::Legacy);
	}
}

TEST_CASE("parameters that are not a board suppress everything downstream", "[camera][flow]")
{
	reset();
	flow::Graph graph;
	const flow::NodeId spec = graph.add<camera::BoardSpecificationNode>();
	const flow::NodeId render = graph.add<camera::RenderBoardNode>();
	REQUIRE(graph.connect(output(graph, spec), flow::PortAddress{render, graph.node(render).input(0).id()}) ==
			flow::Connection::Ok);

	const std::string name = GENERATE(std::string{"squaresX"}, std::string{"squareLengthMm"}, std::string{"identity"});
	if (name == "squaresX")
		setParam(graph, spec, name, -3); // negative is too few, not a wrapped huge count
	else if (name == "squareLengthMm")
		setParam(graph, spec, name, 0.0f);
	else
		setParam(graph, spec, name, std::string{});

	flow::Evaluation evaluation{graph};
	flow::SerialScheduler{}.run(graph, evaluation);
	INFO("bad parameter: " << name);
	CHECK(evaluation.value(output(graph, spec)).empty());
	CHECK(evaluation.value(output(graph, render, 0)).empty());
}

TEST_CASE("renderBoard draws the specification's pattern", "[camera][flow]")
{
	reset();
	flow::Graph graph;
	const flow::NodeId spec = graph.add<camera::BoardSpecificationNode>();
	const flow::NodeId render = graph.add<camera::RenderBoardNode>();
	REQUIRE(graph.connect(output(graph, spec), flow::PortAddress{render, graph.node(render).input(0).id()}) ==
			flow::Connection::Ok);
	setParam(graph, render, "pixelsPerSquare", 40);
	setParam(graph, render, "marginPixels", 10);

	flow::Evaluation evaluation{graph};
	flow::SerialScheduler{}.run(graph, evaluation);
	const image::Image& raster = evaluation.value(output(graph, render, 0)).get<image::Image>();
	CHECK(raster.width() == 7 * 40 + 20);
	CHECK(raster.height() == 5 * 40 + 20);
	const std::string& description = evaluation.value(output(graph, render, 1)).get<std::string>();
	const board::Specification& board = evaluation.value(output(graph, spec)).get<board::Specification>();
	CHECK(description.rfind(board.pattern().description(), 0) == 0);

	SECTION("a render that cannot happen clears both outputs")
	{
		setParam(graph, render, "pixelsPerSquare", 0);
		flow::SerialScheduler{}.run(graph, evaluation);
		CHECK(evaluation.value(output(graph, render, 0)).empty());
		CHECK(evaluation.value(output(graph, render, 1)).empty());
	}
}

TEST_CASE("detectBoard reports for the frame it is given", "[camera][flow]")
{
	reset();
	flow::Graph graph;
	const flow::NodeId spec = sceneBoard(graph);
	const flow::NodeId detect = graph.add<camera::DetectBoardNode>();
	REQUIRE(graph.connect(output(graph, spec), flow::PortAddress{detect, graph.node(detect).input(1).id()}) ==
			flow::Connection::Ok);
	const flow::PortAddress imagePin = feed<image::Image>(graph, "image", detect, 0);
	const image::Image blank{960, 720, image::PixelFormat::Gray8, image::ColorSpace::sRGB};

	SECTION("with a frame wired, the observation names it")
	{
		const flow::PortAddress framePin = feed<media::FrameRef>(graph, "frame", detect, 2);
		flow::Evaluation evaluation{graph};
		bind(evaluation, imagePin, blank);
		const media::FrameRef frame{core::Uri{"/synthetic/calibration"}, 3, core::Time{}};
		bind(evaluation, framePin, frame);
		flow::SerialScheduler{}.run(graph, evaluation);

		const board::DetectionReport& report = evaluation.value(output(graph, detect)).get<board::DetectionReport>();
		REQUIRE(report.status == board::DetectionStatus::Detected);
		CHECK(report.observation->frame == frame);
		CHECK(report.observation->features.size() == 24);
	}

	SECTION("unwired, the detection still runs and names no frame")
	{
		flow::Evaluation evaluation{graph};
		bind(evaluation, imagePin, blank);
		flow::SerialScheduler{}.run(graph, evaluation);
		const board::DetectionReport& report = evaluation.value(output(graph, detect)).get<board::DetectionReport>();
		REQUIRE(report.observation.has_value());
		CHECK(report.observation->frame == media::FrameRef{});
	}

	SECTION("the settings reach the request")
	{
		setParam(graph, detect, "longestSide", 640);
		setParam(graph, detect, "refineNative", false);
		setParam(graph, detect, "minimumCorners", 9);
		flow::Evaluation evaluation{graph};
		bind(evaluation, imagePin, blank);
		flow::SerialScheduler{}.run(graph, evaluation);
		const board::DetectionRequest& request =
			evaluation.value(output(graph, detect)).get<board::DetectionReport>().request;
		REQUIRE(std::holds_alternative<board::LongestSide>(request.scale));
		CHECK(std::get<board::LongestSide>(request.scale).pixels == 640);
		CHECK_FALSE(request.refineAtNativeResolution);
		CHECK(request.minimumCorners == 9);
	}
}

TEST_CASE("calibrateCamera hands its settings to the method", "[camera][flow]")
{
	reset();
	SECTION("the defaults are calibration::Request's")
	{
		const Report report = calibrateScene([](flow::Graph&, flow::NodeId) {});
		REQUIRE(report.status == camera::calibration::CalibrationStatus::Succeeded);
		CHECK(report.verdict == camera::calibration::Verdict::Ready);
		const camera::calibration::Request& asked = report.reproducibility.request;
		const camera::calibration::Request defaults;
		CHECK(asked.model == defaults.model);
		CHECK(asked.fitnessProfile == defaults.fitnessProfile);
		CHECK(asked.maximumViews == defaults.maximumViews);
		CHECK(asked.heldOutFraction == defaults.heldOutFraction);
		CHECK(asked.resamples == defaults.resamples);
		CHECK(asked.seed == defaults.seed);
		CHECK(asked.execution == camera::calibration::ExecutionPolicy::Normal);
		CHECK(std::holds_alternative<board::NativeScale>(asked.detection.scale));
		CHECK(asked.detection.minimumCorners == defaults.detection.minimumCorners);
	}

	SECTION("every parameter reaches the request")
	{
		const Report report = calibrateScene(
			[](flow::Graph& graph, flow::NodeId id)
			{
				setParam(graph, id, "maximumViews", 12);
				setParam(graph, id, "heldOutFraction", 0.25f);
				setParam(graph, id, "resamples", 3);
				setParam(graph, id, "seed", -1);
				setParam(graph, id, "deterministic", true);
				setParam(graph, id, "longestSide", 800);
				setParam(graph, id, "refineNative", false);
				setParam(graph, id, "minimumCorners", 6);
			});
		const camera::calibration::Request& asked = report.reproducibility.request;
		CHECK(asked.maximumViews == 12);
		CHECK(asked.heldOutFraction == 0.25);
		CHECK(asked.resamples == 3);
		CHECK(asked.seed == 0xFFFFFFFFu); // a seed keeps its bits: -1 is a seed, not a clamp to 0
		CHECK(asked.execution == camera::calibration::ExecutionPolicy::DeterministicDebug);
		REQUIRE(std::holds_alternative<board::LongestSide>(asked.detection.scale));
		CHECK(std::get<board::LongestSide>(asked.detection.scale).pixels == 800);
		CHECK_FALSE(asked.detection.refineAtNativeResolution);
		CHECK(asked.detection.minimumCorners == 6);
		CHECK(report.diagnostics.viewsSelected == 12);
	}

	SECTION("a model the estimator cannot estimate is a failed report, not a substitute")
	{
		const Report report = calibrateScene([](flow::Graph& graph, flow::NodeId id)
											 { setParam(graph, id, "model", camera::DistortionModel::KannalaBrandt4); });
		CHECK(report.status == camera::calibration::CalibrationStatus::Failed);
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures.front().failure == camera::calibration::Failure::NoEstimator);
	}
}

TEST_CASE("an imported model takes part through the node's policy", "[camera][flow]")
{
	reset();
	flow::Graph graph;
	const flow::NodeId spec = sceneBoard(graph);
	const flow::NodeId calibrate = graph.add<camera::CalibrateCameraNode>();
	REQUIRE(graph.connect(output(graph, spec), flow::PortAddress{calibrate, graph.node(calibrate).input(1).id()}) ==
			flow::Connection::Ok);
	const flow::PortAddress footagePin = feed<media::FrameSequence>(graph, "footage", calibrate, 0);
	const flow::PortAddress importedPin = feed<CameraModel>(graph, "imported", calibrate, 2);
	setParam(graph, calibrate, "importedPolicy", camera::calibration::ImportedModelPolicy::HoldAndValidate);

	flow::Evaluation evaluation{graph};
	bind(evaluation, footagePin, footage(scene().frames));
	const CameraModel truth = *CameraModel::create(trueCamera()).model;
	bind(evaluation, importedPin, truth);
	flow::SerialScheduler{}.run(graph, evaluation);

	const Report& report = evaluation.value(output(graph, calibrate)).get<Report>();
	REQUIRE(report.reproducibility.request.imported.has_value());
	CHECK(report.reproducibility.request.importedPolicy == camera::calibration::ImportedModelPolicy::HoldAndValidate);
	REQUIRE(report.model.has_value());
	CHECK(report.model->intrinsics().fx == truth.intrinsics().fx); // held exactly, never estimated
	CHECK(script().estimates == 0);
}

TEST_CASE("cameraModel passes a calibration's model on, and suppresses without one", "[camera][flow]")
{
	reset();
	flow::Graph graph;
	const flow::NodeId spec = sceneBoard(graph);
	const flow::NodeId calibrate = graph.add<camera::CalibrateCameraNode>();
	const flow::NodeId model = graph.add<camera::CameraModelNode>();
	REQUIRE(graph.connect(output(graph, spec), flow::PortAddress{calibrate, graph.node(calibrate).input(1).id()}) ==
			flow::Connection::Ok);
	REQUIRE(graph.connect(output(graph, calibrate), flow::PortAddress{model, graph.node(model).input(0).id()}) ==
			flow::Connection::Ok);
	const flow::PortAddress footagePin = feed<media::FrameSequence>(graph, "footage", calibrate, 0);
	setParam(graph, calibrate, "resamples", 2);

	flow::Evaluation evaluation{graph};
	bind(evaluation, footagePin, footage(scene().frames));
	flow::SerialScheduler{}.run(graph, evaluation);
	const flow::PortValue& found = evaluation.value(output(graph, model));
	REQUIRE(found.holds<CameraModel>());
	CHECK(found.get<CameraModel>().intrinsics().fx == trueCamera().intrinsics.fx);

	// The report names its verdict first, which is what a host shows for the port.
	CHECK(evaluation.describe(output(graph, calibrate)).rfind("Ready: ", 0) == 0);

	SECTION("a failed calibration clears the model")
	{
		script().answer = [](const std::vector<board::Observation>&)
		{ return std::optional<camera::CameraModelParameters>{}; };
		evaluation.requestRecompute(calibrate);
		flow::SerialScheduler{}.run(graph, evaluation);
		CHECK(evaluation.value(output(graph, calibrate)).get<Report>().status ==
			  camera::calibration::CalibrationStatus::Failed);
		CHECK(evaluation.value(output(graph, model)).empty());
		CHECK(evaluation.describe(output(graph, calibrate)).rfind("Failed: EstimationFailed", 0) == 0);
	}
}
