// The registration contracts that need no backend: the camera observation graph (components,
// bridges, weak bridges), the registration/1 fitness profile, and the report's reading of itself.

#include <lain/camera/registration/fitness.h>
#include <lain/camera/registration/observationgraph.h>
#include <lain/camera/registration/report.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using namespace lain;
using namespace lain::camera::registration;

namespace
{
	using Groups = std::vector<std::vector<std::uint32_t>>;

	// The edge between a and b, or nullptr.
	const GraphEdge* edge(const ObservationGraph& graph, std::uint32_t a, std::uint32_t b)
	{
		for (const GraphEdge& e : graph.edges)
		{
			if (e.a == a && e.b == b)
				return &e;
		}
		return nullptr;
	}

	// `count` copies of one group.
	Groups repeated(std::vector<std::uint32_t> members, std::size_t count)
	{
		return Groups(count, std::move(members));
	}

	Groups operator+(Groups a, const Groups& b)
	{
		a.insert(a.end(), b.begin(), b.end());
		return a;
	}
} // namespace

TEST_CASE("a fully connected rig has one component and no bridge", "[camera][registration]")
{
	// Three cameras around one board: every group seen by all three.
	const ObservationGraph graph = observationGraph(3, repeated({0, 1, 2}, 4));
	REQUIRE(graph.components.size() == 1);
	CHECK(graph.components[0] == std::vector<std::uint32_t>{0, 1, 2});
	REQUIRE(graph.edges.size() == 3);
	for (const GraphEdge& e : graph.edges)
	{
		CHECK(e.shared == 4);
		CHECK_FALSE(e.bridge);
	}
	CHECK(graph.sharedPerCamera == std::vector<std::uint32_t>{4, 4, 4});
	CHECK(weakBridges(graph, 100).empty());
}

TEST_CASE("a partially overlapping rig is connected through a chain of bridges", "[camera][registration]")
{
	// A row of cameras where each sees only its neighbours' part of the room: 0-1, 1-2, 2-3.
	const ObservationGraph graph =
		observationGraph(4, repeated({0, 1}, 6) + repeated({1, 2}, 3) + repeated({2, 3}, 8) + repeated({3}, 5));
	REQUIRE(graph.components.size() == 1);
	REQUIRE(graph.edges.size() == 3);
	CHECK(edge(graph, 0, 1)->shared == 6);
	CHECK(edge(graph, 1, 2)->shared == 3);
	CHECK(edge(graph, 2, 3)->shared == 8);
	for (const GraphEdge& e : graph.edges)
		CHECK(e.bridge);
	// A group only camera 3 saw connects nothing and counts for nothing.
	CHECK(graph.sharedPerCamera == std::vector<std::uint32_t>{6, 9, 11, 8});

	SECTION("the bridges below a threshold are weak")
	{
		const std::vector<GraphEdge> weak = weakBridges(graph, 5);
		REQUIRE(weak.size() == 1);
		CHECK(weak[0].a == 1);
		CHECK(weak[0].b == 2);
	}
}

TEST_CASE("a weak link inside a cycle is not a bridge", "[camera][registration]")
{
	// A ring 0-1-2-3-0 where 1-2 rests on one group: losing it reroutes through 0 and 3.
	const ObservationGraph graph = observationGraph(
		4, repeated({0, 1}, 6) + repeated({1, 2}, 1) + repeated({2, 3}, 6) + repeated({3, 0}, 6));
	REQUIRE(graph.components.size() == 1);
	for (const GraphEdge& e : graph.edges)
		CHECK_FALSE(e.bridge);
	CHECK(weakBridges(graph, 5).empty());
}

TEST_CASE("a rig that never saw the board together falls apart into components", "[camera][registration]")
{
	// Cameras 0 and 1 share a view, 2 and 3 share another, and 4 only ever saw the board alone.
	const ObservationGraph graph = observationGraph(5, repeated({3, 2}, 2) + repeated({1, 0}, 2) + repeated({4}, 3));
	REQUIRE(graph.components.size() == 3);
	CHECK(graph.components[0] == std::vector<std::uint32_t>{0, 1});
	CHECK(graph.components[1] == std::vector<std::uint32_t>{2, 3});
	CHECK(graph.components[2] == std::vector<std::uint32_t>{4});
	CHECK(graph.sharedPerCamera[4] == 0);
	// Each component's single edge is a bridge within it.
	CHECK(edge(graph, 0, 1)->bridge);
	CHECK(edge(graph, 2, 3)->bridge);
}

TEST_CASE("a camera listed twice in a group is one camera", "[camera][registration]")
{
	const ObservationGraph graph = observationGraph(2, {{1, 0, 1}, {0, 0}});
	REQUIRE(graph.edges.size() == 1);
	CHECK(graph.edges[0].shared == 1);
	CHECK(graph.sharedPerCamera == std::vector<std::uint32_t>{1, 1});
}

TEST_CASE("bridges are found without recursion on a very long chain", "[camera][registration]")
{
	// A recursive depth-first search over 200,000 cameras in a line would overflow a thread's stack.
	const std::uint32_t n = 200000;
	Groups groups;
	for (std::uint32_t c = 0; c + 1 < n; ++c)
		groups.push_back({c, c + 1});
	const ObservationGraph graph = observationGraph(n, groups);
	CHECK(graph.components.size() == 1);
	REQUIRE(graph.edges.size() == n - 1);
	std::size_t bridges = 0;
	for (const GraphEdge& e : graph.edges)
		bridges += e.bridge ? 1 : 0;
	CHECK(bridges == n - 1);
}

TEST_CASE("both registration profiles are known, and overrides change only their Ready tier", "[camera][registration]")
{
	CHECK(fitnessProfileNames() == std::vector<std::string>{"registration/1", "registration-targetless/1"});
	CHECK_FALSE(fitnessProfile("registration/2").has_value());
	CHECK_FALSE(fitnessProfile("").has_value()); // the empty name is a request's, resolved by the method
	const std::optional<FitnessProfile> targetless = fitnessProfile("registration-targetless/1");
	REQUIRE(targetless.has_value());
	CHECK(targetless->name == "registration-targetless/1");
	CHECK(targetless->unit == EvidenceUnit::Track); // it counts accepted feature tracks
	const std::optional<FitnessProfile> profile = fitnessProfile("registration/1");
	REQUIRE(profile.has_value());
	CHECK(profile->name == "registration/1");
	CHECK(profile->unit == EvidenceUnit::CaptureGroup); // it counts capture groups
	// Ready is stricter than Exploratory on every criterion, in both.
	for (const FitnessProfile& p : {*profile, *targetless})
	{
		CHECK(p.ready.minimumSharedPerCamera > p.exploratory.minimumSharedPerCamera);
		CHECK(p.ready.minimumBridgeShared > p.exploratory.minimumBridgeShared);
		CHECK(p.ready.maximumHeldOutAngle < p.exploratory.maximumHeldOutAngle);
		CHECK(p.ready.maximumRotationVariation < p.exploratory.maximumRotationVariation);
		CHECK(p.ready.maximumTranslationVariation < p.exploratory.maximumTranslationVariation);
		CHECK(p.ready.maximumOutlierFraction < p.exploratory.maximumOutlierFraction);
	}

	FitnessOverrides overrides;
	overrides.minimumBridgeShared = 2;
	overrides.maximumHeldOutAngle = 0.003;
	const FitnessProfile resolved = resolve(*profile, overrides);
	CHECK(resolved.ready.minimumBridgeShared == 2);
	CHECK(resolved.ready.maximumHeldOutAngle == 0.003);
	CHECK(resolved.ready.minimumSharedPerCamera == profile->ready.minimumSharedPerCamera);
	CHECK(resolved.exploratory.minimumBridgeShared == profile->exploratory.minimumBridgeShared);
	CHECK(resolved.exploratory.maximumHeldOutAngle == profile->exploratory.maximumHeldOutAngle);
	CHECK(resolved.unit == profile->unit);
}

TEST_CASE("a registration report reads its own result", "[camera][registration]")
{
	SECTION("a failed report says why first")
	{
		Report report;
		report.failures.push_back({Failure::Disconnected, "2 components"});
		report.failures.push_back({Failure::TooFewCameras, "never"});
		CHECK(report.toString() == "Failed: Disconnected (2 components)");
		CHECK_FALSE(report.referenceFromCamera(camera::capture::CameraIdentity{"a"}).has_value());
	}
	SECTION("a registered camera's transform is looked up by identity")
	{
		Report report;
		report.status = RegistrationStatus::Succeeded;
		report.verdict = camera::Verdict::Exploratory;
		report.reference = camera::capture::CameraIdentity{"a"};
		report.scale = {Scale::Metric, "the board's measured square"};
		const math::RigidTransformd moved{math::Quatd{1, 0, 0, 0}, math::Vec3d{0.5, 0, 0}};
		report.cameras = {{camera::capture::CameraIdentity{"a"}, math::RigidTransformd{}},
						  {camera::capture::CameraIdentity{"b"}, moved}};
		BoardDiagnostics& board = std::get<BoardDiagnostics>(report.diagnostics.method);
		board.groupsExamined = 12;
		board.groupsUsable = 10;
		report.fitnessNotes = {"held-out angle 2.1 mrad above 1 mrad"};
		const std::optional<math::RigidTransformd> b = report.referenceFromCamera(camera::capture::CameraIdentity{"b"});
		REQUIRE(b.has_value());
		CHECK(b->translation() == math::Vec3d{0.5, 0, 0});
		CHECK_FALSE(report.referenceFromCamera(camera::capture::CameraIdentity{"c"}).has_value());
		CHECK(report.toString() ==
			  "Exploratory: 2 cameras relative to a, metric, no held-out evidence (not computed), 10 of 12 groups "
			  "usable; short of the next verdict: held-out angle 2.1 mrad above 1 mrad");
	}
}
