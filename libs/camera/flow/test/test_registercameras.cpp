// The registerCameras node through a real graph and the production scheduler, over the registration
// method module's stand-in rig (syntheticrig.h): a detector and a pose solver answering from a known
// truth, and a refiner handing back its start. What is checked is the NODE: that it pairs footage
// with models, names each camera by its footage's source, groups by position, and hands the method
// what its parameters say. Registration itself is the method module's and the Ceres plugin's tests'.
// Its own executable: these stand-ins share keys with the calibration scene's.

#include "syntheticrig.h"

#include <lain/camera/flow/boardspecificationnode.h>
#include <lain/camera/flow/register.h>
#include <lain/camera/flow/registercamerasnode.h>
#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera::testing;
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

	flow::PortAddress output(const flow::Graph& graph, flow::NodeId id, std::size_t index = 0)
	{
		return {id, graph.node(id).output(index).id()};
	}

	// boardSpecification (the rig's board: 7x5, 24 mm) -> registerCameras, with the rig's footage and
	// models bound as the two collections, `configure` applied to the node; the report, run.
	template <typename Configure>
	Report registerRig(Configure configure, std::size_t dropModels = 0)
	{
		flow::Graph graph;
		const flow::NodeId spec = graph.add<camera::BoardSpecificationNode>();
		setParam(graph, spec, "squareLengthMm", 24.0f);
		const flow::NodeId node = graph.add<camera::RegisterCamerasNode>();
		REQUIRE(graph.connect(output(graph, spec), flow::PortAddress{node, graph.node(node).input(2).id()}) ==
				flow::Connection::Ok);
		flow::GroupInputNode& in = graph.boundaryInputNode();
		const flow::PortId footagePin = in.addBoundary<std::vector<media::FrameSequence>>("footage");
		const flow::PortId modelsPin = in.addBoundary<std::vector<camera::CameraModel>>("models");
		REQUIRE(graph.connect(flow::PortAddress{in.id(), footagePin}, flow::PortAddress{node, graph.node(node).input(0).id()}) ==
				flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{in.id(), modelsPin}, flow::PortAddress{node, graph.node(node).input(1).id()}) ==
				flow::Connection::Ok);
		configure(graph, node);

		std::vector<media::FrameSequence> footage;
		std::vector<camera::CameraModel> models;
		for (const camera::registration::board::RigFootage& c : rig::footage())
		{
			footage.push_back(c.footage);
			models.push_back(c.model);
		}
		models.resize(models.size() - dropModels, models.front());

		flow::Evaluation evaluation{graph};
		flow::PortValue f, m;
		f.set(std::move(footage));
		m.set(std::move(models));
		evaluation.bind({in.id(), footagePin}, std::move(f));
		evaluation.bind({in.id(), modelsPin}, std::move(m));
		flow::SerialScheduler{}.run(graph, evaluation);
		const flow::PortValue& report = evaluation.value(output(graph, node));
		REQUIRE(report.holds<Report>());
		return report.get<Report>();
	}
} // namespace

TEST_CASE("registerCameras pairs footage with models and names each camera by its source", "[camera][flow]")
{
	rig::reset(3, 20);
	rig::registerPassThroughRefiner();
	const Report report = registerRig([](flow::Graph&, flow::NodeId) {});
	REQUIRE(report.status == camera::registration::RegistrationStatus::Succeeded);
	REQUIRE(report.cameras.size() == 3);
	CHECK(report.cameras[0].camera.value == "/rig/cam00");
	CHECK(report.cameras[1].camera.value == "/rig/cam01");
	CHECK(report.cameras[2].camera.value == "/rig/cam02");
	CHECK(report.diagnostics.groupsExamined == 20); // frame k of every camera, group k
	CHECK(report.reference->value == "/rig/cam00");
}

TEST_CASE("registerCameras hands its settings to the method", "[camera][flow]")
{
	rig::reset(3, 20);
	rig::registerPassThroughRefiner();
	SECTION("the defaults are registration::Request's")
	{
		const Report report = registerRig([](flow::Graph&, flow::NodeId) {});
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
		CHECK(std::holds_alternative<camera::board::NativeScale>(asked.detection.scale));
		CHECK(asked.detection.minimumCorners == defaults.detection.minimumCorners);
	}
	SECTION("every parameter reaches the request")
	{
		const Report report = registerRig(
			[](flow::Graph& graph, flow::NodeId id)
			{
				setParam(graph, id, "reference", std::string("/rig/cam02"));
				setParam(graph, id, "unknownApplicability", camera::registration::ApplicabilityPolicy::Accept);
				setParam(graph, id, "pixelSigma", 0.7f);
				setParam(graph, id, "loss", camera::registration::LossFamily::Huber);
				setParam(graph, id, "lossScale", 2.5f);
				setParam(graph, id, "heldOutFraction", 0.25f);
				setParam(graph, id, "resamples", 3);
				setParam(graph, id, "seed", -1);
				setParam(graph, id, "maximumIterations", 40);
				setParam(graph, id, "deterministic", true);
				setParam(graph, id, "longestSide", 800);
				setParam(graph, id, "refineNative", false);
				setParam(graph, id, "minimumCorners", 6);
			});
		const camera::registration::Request& asked = report.reproducibility.request;
		REQUIRE(asked.reference.has_value());
		CHECK(asked.reference->value == "/rig/cam02");
		CHECK(report.reference->value == "/rig/cam02");
		CHECK(asked.noise.pixelSigma == 0.7);
		CHECK(asked.loss.family == camera::registration::LossFamily::Huber);
		CHECK(asked.loss.scale == 2.5);
		CHECK(asked.heldOutFraction == 0.25);
		CHECK(asked.resamples == 3);
		CHECK(asked.seed == 0xFFFFFFFFu); // a seed keeps its bits
		CHECK(asked.maximumIterations == 40);
		CHECK(asked.execution == camera::ExecutionPolicy::DeterministicDebug);
		REQUIRE(std::holds_alternative<camera::board::LongestSide>(asked.detection.scale));
		CHECK(std::get<camera::board::LongestSide>(asked.detection.scale).pixels == 800);
		CHECK_FALSE(asked.detection.refineAtNativeResolution);
		CHECK(asked.detection.minimumCorners == 6);
	}
	SECTION("a refusing applicability policy reaches the method")
	{
		const Report report = registerRig(
			[](flow::Graph& graph, flow::NodeId id)
			{ setParam(graph, id, "unknownApplicability", camera::registration::ApplicabilityPolicy::Refuse); });
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures.front().failure == camera::registration::Failure::UnknownApplicability);
	}
}

TEST_CASE("registerCameras refuses footage and models that do not pair", "[camera][flow]")
{
	rig::reset(3, 20);
	rig::registerPassThroughRefiner();
	const Report report = registerRig([](flow::Graph&, flow::NodeId) {}, 1);
	CHECK(report.status == camera::registration::RegistrationStatus::Failed);
	REQUIRE_FALSE(report.failures.empty());
	CHECK(report.failures.front().failure == camera::registration::Failure::InvalidDataset);
	CHECK(report.failures.front().detail == "3 cameras' footage and 2 camera models: they pair by position");
}
