// The calibrateCameraTargetless node through a real graph and the production scheduler, over the
// targetless method module's stand-in scene (syntheticscene.h): an extractor, a matcher and a geometry
// solver answering from a known moving camera, and a refiner handing back its start
// (passthroughrefiner.h). What is checked is the NODE: that it hands the method what its parameters
// say, names the camera by where its footage lies, and passes on what comes back. Calibration itself is
// the method module's and the plugins' tests'.
//
// Its own executable, as each method node's test has: these stand-ins answer from a process-wide scene
// of their own.

#include "passthroughrefiner.h"
#include "syntheticscene.h"
#include "targetlesscalibrationtext.h"

#include <lain/camera/calibration/targetless.h>
#include <lain/camera/flow/calibratecameratargetlessnode.h>
#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <variant>

using namespace lain;
namespace scene = lain::camera::testing::scene;
namespace standin = lain::camera::testing;
using camera::calibration::Report;
namespace targetless = lain::camera::calibration::targetless;

namespace
{
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

	// The scene's moving camera wandering through 120 landmarks over twelve frames, the stand-ins
	// registered, and the refiner saying it estimates every model a case asks for: what the method
	// module's own cases calibrate.
	void setUp()
	{
		scene::registerStandIns();
		standin::registerPassThroughRefiner();
		scene::resetWandering(12, 120);
		std::lock_guard<std::mutex> lock(standin::refinerScript().mutex);
		standin::refinerScript().asked.clear();
		standin::refinerScript().estimates = {camera::DistortionModel::None, camera::DistortionModel::BrownConrady5};
		standin::refinerScript().finalCost = nullptr;
	}

	// calibrateCameraTargetless with `footage` bound (the scene's when not given), `imported` wired when
	// given, and `configure` applied to the node; the report, run.
	template <typename Configure>
	Report calibrateScene(Configure configure, std::optional<media::FrameSequence> footage = std::nullopt,
						  std::optional<camera::CameraModel> imported = std::nullopt)
	{
		flow::Graph graph;
		const flow::NodeId node = graph.add<camera::CalibrateCameraTargetlessNode>();
		flow::GroupInputNode& in = graph.boundaryInputNode();
		const flow::PortId footagePin = in.addBoundary<media::FrameSequence>("footage");
		REQUIRE(graph.connect(flow::PortAddress{in.id(), footagePin}, flow::PortAddress{node, graph.node(node).input(0).id()}) ==
				flow::Connection::Ok);
		flow::PortId importedPin;
		if (imported)
		{
			importedPin = in.addBoundary<camera::CameraModel>("imported");
			REQUIRE(graph.connect(flow::PortAddress{in.id(), importedPin},
								  flow::PortAddress{node, graph.node(node).input(1).id()}) == flow::Connection::Ok);
		}
		configure(graph, node);

		flow::Evaluation evaluation{graph};
		// Set in two branches, not through a conditional's temporary: GCC 13 at -O3 calls the
		// temporary copied from an empty `footage` maybe uninitialized, though that arm is never taken
		// (CI, 2026-10-21).
		flow::PortValue f;
		if (footage)
			f.set(*footage);
		else
			f.set(scene::sequenceFootage().footage);
		evaluation.bind({in.id(), footagePin}, std::move(f));
		if (imported)
		{
			flow::PortValue m;
			m.set(*imported);
			evaluation.bind({in.id(), importedPin}, std::move(m));
		}
		flow::SerialScheduler{}.run(graph, evaluation);
		const flow::PortValue& report = evaluation.value(flow::PortAddress{node, graph.node(node).output(0).id()});
		REQUIRE(report.holds<Report>());
		return report.get<Report>();
	}

	const targetless::Settings& settingsOf(const Report& report)
	{
		REQUIRE(std::holds_alternative<camera::calibration::TargetlessRecord>(report.reproducibility.method));
		return std::get<camera::calibration::TargetlessRecord>(report.reproducibility.method).settings;
	}

	const camera::calibration::TargetlessDiagnostics& targetlessOf(const Report& report)
	{
		REQUIRE(std::holds_alternative<camera::calibration::TargetlessDiagnostics>(report.diagnostics.method));
		return std::get<camera::calibration::TargetlessDiagnostics>(report.diagnostics.method);
	}
} // namespace

TEST_CASE("calibrateCameraTargetless hands its settings to the method", "[camera][flow]")
{
	setUp();

	SECTION("the defaults are calibration::Request's and calibration::targetless::Settings'")
	{
		const Report report = calibrateScene([](flow::Graph&, flow::NodeId) {});
		INFO(report.toString());
		const camera::calibration::Request& asked = report.reproducibility.request;
		const camera::calibration::Request defaults;
		CHECK(asked.model == defaults.model);
		CHECK_FALSE(asked.imported.has_value());
		CHECK(asked.importedPolicy == defaults.importedPolicy);
		CHECK(asked.fitnessProfile == defaults.fitnessProfile);
		CHECK(asked.heldOutFraction == defaults.heldOutFraction);
		CHECK(asked.resamples == defaults.resamples);
		CHECK(asked.seed == defaults.seed);
		CHECK(asked.execution == camera::calibration::ExecutionPolicy::Normal);
		// Empty is the method's own, which the thresholds name.
		CHECK(report.thresholds.name == "reconstruction-targetless/1");

		const targetless::Settings& ran = settingsOf(report);
		const targetless::Settings library;
		const camera::feature::SequenceExtractionRequest& extraction = ran.extraction;
		REQUIRE(std::holds_alternative<camera::LongestSide>(extraction.scale));
		CHECK(std::get<camera::LongestSide>(extraction.scale).pixels == 1920);
		CHECK(extraction.samples == library.extraction.samples);
		CHECK(extraction.featureCap == library.extraction.featureCap);
		CHECK(extraction.featureCap == 2048); // a moving camera's, not the fixed rig's 8192
		CHECK(extraction.matching.search == library.extraction.matching.search);
		CHECK(extraction.matching.ratio == library.extraction.matching.ratio); // 0.8 as typed, not 0.800000012
		CHECK(extraction.threshold == library.extraction.threshold);
		CHECK(extraction.minimumPairInliers == library.extraction.minimumPairInliers);
		CHECK(ran.loss.family == library.loss.family);
		CHECK(ran.loss.scale == library.loss.scale);
		CHECK(ran.freePrincipalPoint == library.freePrincipalPoint);
		CHECK(ran.freePrincipalPoint);
		// What the node does not expose stays the library's: it describes the extractor, not the footage.
		CHECK(extraction.prescreenFeatures == library.extraction.prescreenFeatures);
		CHECK(extraction.prescreenMatches == library.extraction.prescreenMatches);
		CHECK(extraction.localisationPerSize == library.extraction.localisationPerSize);
		CHECK(extraction.seed == library.extraction.seed);
		CHECK(extraction.execution == camera::ExecutionPolicy::Normal);
		// It calibrated what the scene's footage holds.
		CHECK(targetlessOf(report).frames.size() == 12);
	}

	SECTION("every parameter reaches the request and the settings")
	{
		// Each float a value a float does not hold exactly, so each is read as the decimal typed only if
		// the node rounds it to one. HoldAndValidate with nothing to hold is refused before any frame is
		// decoded, and the report says what it was asked all the same.
		const Report report = calibrateScene(
			[](flow::Graph& graph, flow::NodeId id)
			{
				setParam(graph, id, "model", camera::DistortionModel::None);
				setParam(graph, id, "importedPolicy", camera::calibration::ImportedModelPolicy::HoldAndValidate);
				setParam(graph, id, "fitnessProfile", std::string{"reconstruction/1"});
				setParam(graph, id, "heldOutFraction", 0.3f);
				setParam(graph, id, "resamples", 3);
				setParam(graph, id, "seed", -1);
				setParam(graph, id, "deterministic", true);
				setParam(graph, id, "longestSide", 800);
				setParam(graph, id, "sampledFrames", 9);
				setParam(graph, id, "featureCap", 1000);
				setParam(graph, id, "matchSearch", camera::feature::MatchSearch::Approximate);
				setParam(graph, id, "matchRatio", 0.7f);
				setParam(graph, id, "inlierPixels", 3.3f);
				setParam(graph, id, "minimumPairInliers", 20);
				setParam(graph, id, "loss", camera::refinement::LossFamily::Huber);
				setParam(graph, id, "lossScale", 2.2f);
				setParam(graph, id, "freePrincipalPoint", false);
			});
		INFO(report.toString());
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures.front().failure == camera::calibration::Failure::NoImportedModel);

		const camera::calibration::Request& asked = report.reproducibility.request;
		CHECK(asked.model == camera::DistortionModel::None);
		CHECK(asked.importedPolicy == camera::calibration::ImportedModelPolicy::HoldAndValidate);
		CHECK(asked.fitnessProfile == "reconstruction/1");
		CHECK(asked.heldOutFraction == 0.3);
		CHECK(asked.resamples == 3);
		CHECK(asked.seed == 0xFFFFFFFFu); // a seed keeps its bits: -1 is a seed, not a clamp to 0
		CHECK(asked.execution == camera::calibration::ExecutionPolicy::DeterministicDebug);

		const targetless::Settings& ran = settingsOf(report);
		const camera::feature::SequenceExtractionRequest& extraction = ran.extraction;
		REQUIRE(std::holds_alternative<camera::LongestSide>(extraction.scale));
		CHECK(std::get<camera::LongestSide>(extraction.scale).pixels == 800);
		CHECK(extraction.samples == 9);
		CHECK(extraction.featureCap == 1000);
		CHECK(extraction.matching.search == camera::feature::MatchSearch::Approximate);
		CHECK(extraction.matching.ratio == 0.7);
		CHECK(extraction.threshold == 3.3);
		CHECK(extraction.minimumPairInliers == 20);
		// The calibration's execution policy governs the extraction too, recorded as run.
		CHECK(extraction.execution == camera::ExecutionPolicy::DeterministicDebug);
		CHECK(ran.loss.family == camera::refinement::LossFamily::Huber);
		CHECK(ran.loss.scale == 2.2);
		CHECK_FALSE(ran.freePrincipalPoint);
	}

	SECTION("a longest side of 0 searches each frame natively")
	{
		const Report report =
			calibrateScene([](flow::Graph& graph, flow::NodeId id)
						   { setParam(graph, id, "longestSide", 0); });
		CHECK(std::holds_alternative<camera::NativeScale>(settingsOf(report).extraction.scale));
	}
}

TEST_CASE("calibrateCameraTargetless selects no views, so has no maximumViews", "[camera][flow]")
{
	// A targetless calibration's frames are its extraction's samples (calibration::Request): a
	// maximumViews setting would say something the method never reads.
	const camera::CalibrateCameraTargetlessNode node;
	for (std::size_t i = 0; i < node.paramCount(); ++i)
		CHECK(node.param(i).name() != "maximumViews");
}

TEST_CASE("calibrating through calibrateCameraTargetless is calling the method", "[camera][flow]")
{
	// The node's report is the one targetless::calibrate gives for the same footage, the camera named
	// by where its footage lies, under the settings and request its parameters say: every field but the
	// timings, through the printer the method's own cases compare with.
	setUp();
	const Report node = calibrateScene(
		[](flow::Graph& graph, flow::NodeId id)
		{
			setParam(graph, id, "model", camera::DistortionModel::None);
			setParam(graph, id, "resamples", 4);
		});
	INFO(node.toString());
	REQUIRE(node.status == camera::calibration::CalibrationStatus::Succeeded);

	camera::calibration::Request request;
	request.model = camera::DistortionModel::None;
	request.resamples = 4;
	// The node names the camera by where its footage lies, "cam00" within /scene (capture/location.h).
	const camera::capture::CameraFootage camera{camera::capture::CameraIdentity{"cam00"}, scene::sequenceFootage().footage};
	const Report method = targetless::calibrate(camera, targetless::Settings{}, request);
	standin::targetlesstext::requireSameReport(node, method);
}

TEST_CASE("calibrateCameraTargetless refuses empty footage as the method does", "[camera][flow]")
{
	setUp();
	const Report report = calibrateScene([](flow::Graph&, flow::NodeId) {}, media::FrameSequence{});
	CHECK(report.status == camera::calibration::CalibrationStatus::Failed);
	REQUIRE_FALSE(report.failures.empty());
	CHECK(report.failures.front().failure == camera::calibration::Failure::NoFootage);
	CHECK_FALSE(report.model.has_value());
}

TEST_CASE("an imported model takes part through calibrateCameraTargetless's policy", "[camera][flow]")
{
	setUp();
	const camera::CameraModel truth = scene::model();

	SECTION("held, it is the result exactly, and no start is swept")
	{
		const Report report = calibrateScene(
			[](flow::Graph& graph, flow::NodeId id)
			{
				setParam(graph, id, "model", camera::DistortionModel::None);
				setParam(graph, id, "importedPolicy", camera::calibration::ImportedModelPolicy::HoldAndValidate);
			},
			std::nullopt, truth);
		INFO(report.toString());
		REQUIRE(report.reproducibility.request.imported.has_value());
		REQUIRE(report.model.has_value());
		CHECK(report.model->intrinsics().fx == truth.intrinsics().fx);
		CHECK(report.model->intrinsics().cy == truth.intrinsics().cy);
		CHECK(targetlessOf(report).sweep.empty());
	}

	SECTION("unwired, the focal sweep starts it")
	{
		const Report report = calibrateScene([](flow::Graph& graph, flow::NodeId id)
											 { setParam(graph, id, "model", camera::DistortionModel::None); });
		CHECK_FALSE(report.reproducibility.request.imported.has_value());
		CHECK(targetlessOf(report).sweep.size() == 3);
	}
}
