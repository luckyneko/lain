// The registerCamerasTargetless node through a real graph and the production scheduler, over the
// targetless method module's stand-in scene (syntheticscene.h): an extractor, a matcher and a
// geometry solver answering from a known truth, and a refiner handing back its start
// (passthroughrefiner.h). What is checked is the NODE: that it pairs footage with models, names each
// camera by its footage's source, groups by position, hands the method what its parameters say, and
// that a refusal of its own is a targetless report. Registration itself is the method module's and
// the plugins' tests'.
//
// Its own executable, as registerCameras' node test has: these stand-ins answer from a process-wide
// scene of their own.

#include "passthroughrefiner.h"
#include "syntheticscene.h"

#include <lain/camera/flow/registercamerastargetlessnode.h>
#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
namespace scene = lain::camera::testing::scene;
using camera::registration::Report;

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

	// The scene's four cameras over 300 static landmarks, nine frames each: what the method module's
	// footage case registers.
	void setUp()
	{
		scene::registerStandIns();
		camera::testing::registerPassThroughRefiner();
		scene::reset(4, 300);
	}

	// registerCamerasTargetless, with the scene's footage and models bound as the two collections and
	// `configure` applied to the node; the report, run. `footage` replaces the scene's when given.
	template <typename Configure>
	Report registerScene(Configure configure, std::size_t dropModels = 0, std::vector<media::FrameSequence> footage = {})
	{
		flow::Graph graph;
		const flow::NodeId node = graph.add<camera::RegisterCamerasTargetlessNode>();
		flow::GroupInputNode& in = graph.boundaryInputNode();
		const flow::PortId footagePin = in.addBoundary<std::vector<media::FrameSequence>>("footage");
		const flow::PortId modelsPin = in.addBoundary<std::vector<camera::CameraModel>>("models");
		REQUIRE(graph.connect(flow::PortAddress{in.id(), footagePin}, flow::PortAddress{node, graph.node(node).input(0).id()}) ==
				flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{in.id(), modelsPin}, flow::PortAddress{node, graph.node(node).input(1).id()}) ==
				flow::Connection::Ok);
		configure(graph, node);

		std::vector<camera::CameraModel> models;
		const bool given = !footage.empty();
		for (const camera::RigFootage& c : scene::footage())
		{
			if (!given)
				footage.push_back(c.footage);
			models.push_back(c.model);
		}
		models.resize(footage.size() - dropModels, models.front());

		flow::Evaluation evaluation{graph};
		flow::PortValue f, m;
		f.set(std::move(footage));
		m.set(std::move(models));
		evaluation.bind({in.id(), footagePin}, std::move(f));
		evaluation.bind({in.id(), modelsPin}, std::move(m));
		flow::SerialScheduler{}.run(graph, evaluation);
		const flow::PortValue& report = evaluation.value(flow::PortAddress{node, graph.node(node).output(0).id()});
		REQUIRE(report.holds<Report>());
		return report.get<Report>();
	}

	const camera::feature::ExtractionRequest& extractionOf(const Report& report)
	{
		REQUIRE(std::holds_alternative<camera::registration::TargetlessRecord>(report.reproducibility.method));
		const auto& record = std::get<camera::registration::TargetlessRecord>(report.reproducibility.method);
		REQUIRE(record.extraction.has_value());
		return *record.extraction;
	}
} // namespace

TEST_CASE("registerCamerasTargetless pairs footage with models and names each camera by its source", "[camera][flow]")
{
	setUp();
	const Report report = registerScene([](flow::Graph&, flow::NodeId) {});
	INFO(report.toString());
	REQUIRE(report.status == camera::registration::RegistrationStatus::Succeeded);
	REQUIRE(report.cameras.size() == 4);
	CHECK(report.cameras[0].camera.value == "/scene/cam00");
	CHECK(report.cameras[1].camera.value == "/scene/cam01");
	CHECK(report.cameras[2].camera.value == "/scene/cam02");
	CHECK(report.cameras[3].camera.value == "/scene/cam03");
	CHECK(report.scale.scale == camera::registration::Scale::Arbitrary);
	// Every camera sampled the same groups, so the frames came by position: frame k of every camera.
	const auto& targetless = std::get<camera::registration::TargetlessDiagnostics>(report.diagnostics.method);
	REQUIRE(targetless.trackSet.has_value());
	CHECK(targetless.trackSet->groups.size() == camera::feature::ExtractionRequest{}.samples);
	CHECK(targetless.tracks == 300);
}

TEST_CASE("registerCamerasTargetless hands its settings to the method", "[camera][flow]")
{
	setUp();
	SECTION("the defaults are registration::Request's and feature::ExtractionRequest's")
	{
		const Report report = registerScene([](flow::Graph&, flow::NodeId) {});
		const camera::registration::Request& asked = report.reproducibility.request;
		const camera::registration::Request defaults;
		CHECK_FALSE(asked.reference.has_value());
		CHECK(asked.unknownApplicability == defaults.unknownApplicability);
		CHECK(asked.noise.pixelSigma == defaults.noise.pixelSigma);
		CHECK(asked.loss.family == defaults.loss.family);
		CHECK(asked.loss.scale == defaults.loss.scale);
		CHECK(asked.fitnessProfile == defaults.fitnessProfile);
		CHECK(asked.heldOutFraction == defaults.heldOutFraction);
		CHECK(asked.resamples == defaults.resamples);
		CHECK(asked.seed == defaults.seed);
		CHECK(asked.maximumIterations == defaults.maximumIterations);
		CHECK(asked.execution == camera::ExecutionPolicy::Normal);
		// The method's own profile, since the empty default asks for it.
		CHECK(report.thresholds.name == "registration-targetless/1");

		const camera::feature::ExtractionRequest& extracted = extractionOf(report);
		const camera::feature::ExtractionRequest d;
		REQUIRE(std::holds_alternative<camera::LongestSide>(extracted.scale));
		CHECK(std::get<camera::LongestSide>(extracted.scale).pixels == std::get<camera::LongestSide>(d.scale).pixels);
		CHECK(extracted.samples == d.samples);
		CHECK(extracted.featureCap == d.featureCap);
		CHECK(extracted.matching.search == d.matching.search);
		CHECK(extracted.matching.ratio == d.matching.ratio);
		CHECK(extracted.geometry.angle == d.geometry.angle); // 4 mrad is exactly 0.004
		CHECK(extracted.minimumPairInliers == d.minimumPairInliers);
		// ... and what the node does not expose stays the library's.
		CHECK(extracted.geometry.seed == d.geometry.seed);
		CHECK(extracted.prescreenFeatures == d.prescreenFeatures);
		CHECK(extracted.prescreenMatches == d.prescreenMatches);
		CHECK(extracted.staticDistance == d.staticDistance);
		CHECK(extracted.staticSizeRatio == d.staticSizeRatio);
		CHECK(extracted.localisationPerSize == d.localisationPerSize);
		CHECK(extracted.execution == camera::ExecutionPolicy::Normal);
	}
	SECTION("every parameter reaches the request")
	{
		const Report report = registerScene(
			[](flow::Graph& graph, flow::NodeId id)
			{
				setParam(graph, id, "reference", std::string("/scene/cam02"));
				setParam(graph, id, "loss", camera::registration::LossFamily::Huber);
				setParam(graph, id, "lossScale", 2.5f);
				setParam(graph, id, "fitnessProfile", std::string("registration-targetless/1"));
				setParam(graph, id, "heldOutFraction", 0.25f);
				setParam(graph, id, "resamples", 3);
				setParam(graph, id, "seed", -1);
				setParam(graph, id, "maximumIterations", 40);
				setParam(graph, id, "longestSide", 800);
				setParam(graph, id, "sampledGroups", 3);
				setParam(graph, id, "featureCap", 1000);
				setParam(graph, id, "matchSearch", camera::feature::MatchSearch::Approximate);
				setParam(graph, id, "matchRatio", 0.7f);
				setParam(graph, id, "inlierAngleMrad", 2.5f);
				setParam(graph, id, "minimumPairInliers", 20);
			});
		INFO(report.toString());
		const camera::registration::Request& asked = report.reproducibility.request;
		REQUIRE(asked.reference.has_value());
		CHECK(asked.reference->value == "/scene/cam02");
		REQUIRE(report.reference.has_value());
		CHECK(report.reference->value == "/scene/cam02");
		CHECK(asked.loss.family == camera::registration::LossFamily::Huber);
		CHECK(asked.loss.scale == 2.5);
		CHECK(asked.fitnessProfile == "registration-targetless/1");
		CHECK(asked.heldOutFraction == 0.25);
		CHECK(asked.resamples == 3);
		CHECK(asked.seed == 0xFFFFFFFFu); // a seed keeps its bits
		CHECK(asked.maximumIterations == 40);

		const camera::feature::ExtractionRequest& extracted = extractionOf(report);
		REQUIRE(std::holds_alternative<camera::LongestSide>(extracted.scale));
		CHECK(std::get<camera::LongestSide>(extracted.scale).pixels == 800);
		CHECK(extracted.samples == 3);
		CHECK(extracted.featureCap == 1000);
		CHECK(extracted.matching.search == camera::feature::MatchSearch::Approximate);
		CHECK(extracted.matching.ratio == 0.7);
		CHECK(extracted.geometry.angle == 0.0025);
		CHECK(extracted.minimumPairInliers == 20);
		// ... and the method ran with them: the groups it sampled, and a registration.
		REQUIRE(report.status == camera::registration::RegistrationStatus::Succeeded);
		const auto& targetless = std::get<camera::registration::TargetlessDiagnostics>(report.diagnostics.method);
		REQUIRE(targetless.trackSet.has_value());
		CHECK(targetless.trackSet->groups.size() == 3);
	}
	SECTION("a native search is a longest side of 0")
	{
		const Report report = registerScene([](flow::Graph& graph, flow::NodeId id)
											{ setParam(graph, id, "longestSide", 0); });
		CHECK(std::holds_alternative<camera::NativeScale>(extractionOf(report).scale));
	}
	SECTION("deterministic reaches the extraction too, which refuses an Approximate search")
	{
		// Two settings reaching the method together: the same work serially, and a search that is
		// reproducible only within a tolerance. The extraction is what refuses the pair.
		const Report report = registerScene(
			[](flow::Graph& graph, flow::NodeId id)
			{
				setParam(graph, id, "deterministic", true);
				setParam(graph, id, "matchSearch", camera::feature::MatchSearch::Approximate);
			});
		INFO(report.toString());
		CHECK(report.reproducibility.request.execution == camera::ExecutionPolicy::DeterministicDebug);
		CHECK(extractionOf(report).execution == camera::ExecutionPolicy::DeterministicDebug);
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures.front().failure == camera::registration::Failure::ExtractionFailed);
		CHECK(report.failures.front().detail.find("approximate") != std::string::npos);
	}
	SECTION("a refusing applicability policy reaches the method")
	{
		const Report report = registerScene(
			[](flow::Graph& graph, flow::NodeId id)
			{ setParam(graph, id, "unknownApplicability", camera::registration::ApplicabilityPolicy::Refuse); });
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures.front().failure == camera::registration::Failure::UnknownApplicability);
	}
}

TEST_CASE("registerCamerasTargetless has no noise model to set", "[camera][flow]")
{
	// Every observation extracted from footage carries its own covariance, so a default noise model
	// would apply to none: a setting that did nothing would only mislead.
	const camera::RegisterCamerasTargetlessNode node;
	for (std::size_t i = 0; i < node.paramCount(); ++i)
		CHECK(node.param(i).name() != "pixelSigma");
}

TEST_CASE("registerCamerasTargetless refuses a rig it cannot make, as a targetless report", "[camera][flow]")
{
	setUp();
	SECTION("footage and models that do not pair")
	{
		const Report report = registerScene([](flow::Graph&, flow::NodeId) {}, 1);
		CHECK(report.status == camera::registration::RegistrationStatus::Failed);
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures.front().failure == camera::registration::Failure::InvalidDataset);
		CHECK(report.failures.front().detail == "4 cameras' footage and 3 camera models: they pair by position");
		// A default Report is a board one. The node's own refusal says it was a targetless
		// registration, with what it would have extracted with, as the method's refusals do.
		CHECK(std::holds_alternative<camera::registration::TargetlessDiagnostics>(report.diagnostics.method));
		CHECK(extractionOf(report).samples == camera::feature::ExtractionRequest{}.samples);
	}
	SECTION("footage that does not group: two cameras with one source")
	{
		const std::vector<camera::RigFootage> rig = scene::footage();
		const Report report = registerScene([](flow::Graph&, flow::NodeId) {}, 0,
											{rig[0].footage, rig[0].footage, rig[1].footage});
		CHECK(report.status == camera::registration::RegistrationStatus::Failed);
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures.front().failure == camera::registration::Failure::InvalidDataset);
		CHECK(report.failures.front().detail.rfind("position 0: ", 0) == 0);
		CHECK(std::holds_alternative<camera::registration::TargetlessDiagnostics>(report.diagnostics.method));
		CHECK(std::holds_alternative<camera::registration::TargetlessRecord>(report.reproducibility.method));
	}
}
