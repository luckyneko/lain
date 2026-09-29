// The Issues panel's rules, apart from the panel (src/validation.cpp). Driver-free: collectIssues is
// a pure function of a definition plus its runtime state, which is the whole reason it was split out
// of the pane — while it lived there, nothing in the suite could reach it, and it spent two
// milestones telling every Blur, Gate, Select and Loop that a defaulted input it seeds ITSELF was a
// missing connection.

#include "validation.h"

#include <lain/flow/boundary.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/example/blurnode.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lain::flow;
using flowview::collectFailures;
using flowview::collectIssues;
using flowview::GraphPath;
using flowview::Issue;
using flowview::PathStep;

namespace
{
	// Does any row mention `fragment`? The rows are display strings, so a substring is what a reader
	// of the panel would actually be looking for.
	bool mentions(const std::vector<Issue>& issues, const std::string& fragment)
	{
		for (const Issue& issue : issues)
		{
			if (issue.message.find(fragment) != std::string::npos)
				return true;
		}
		return false;
	}

	// A source of one image, so a node under test can be given a real incoming edge.
	struct ImageSource : Node
	{
		PortId out;
		ImageSource()
			: Node("Source")
		{
			out = addOutput<lain::image::Image>("image");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<ImageSource>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(lain::image::Image(4, 4, lain::image::PixelFormat::RGBA8));
		}
	};
} // namespace

TEST_CASE("a defaulted input with nothing wired is not a missing required input", "[validation]")
{
	// A default seeds an input that has NO incoming edge, and the port stays Required so that a Gate
	// wired in and turned off still suppresses (ADR-0007). The panel read that Required and called an
	// unwired default a missing connection — but the scheduler fills that slot every run, so it can
	// never be why the node cannot run.
	//
	// Driven through the production BlurNode, which carries both kinds at once: `image` is required
	// with no default, `radius` and `sigma` are required WITH one.
	Graph graph;
	const NodeId blur = graph.add<example::BlurNode>(2, 1.5f);
	Evaluation evaluation{graph};

	const std::vector<Issue> issues = collectIssues(graph, evaluation, GraphPath{});

	// The positive control lives in the same case deliberately: "reports nothing" would also pass if
	// the check had been deleted outright.
	REQUIRE(mentions(issues, "required input 'image' is not connected"));
	REQUIRE_FALSE(mentions(issues, "'radius'"));
	REQUIRE_FALSE(mentions(issues, "'sigma'"));

	// And once the data input IS fed, the node has nothing to report at all.
	const NodeId source = graph.add<ImageSource>();
	REQUIRE(graph.connect(source, 0, blur, 0) == Connection::Ok);
	Evaluation fed{graph};
	REQUIRE_FALSE(mentions(collectIssues(graph, fed, GraphPath{}), "is not connected"));
}

TEST_CASE("a loop reports neither its own count nor its interior's continue", "[validation]")
{
	// The two the defect made unavoidable: EVERY loop reported `count` from the frame it was created,
	// and every loop interior reported `continue`. They sit on different levels — `count` is the
	// loop's own port, `continue` an input of its interior's GroupOutput — so both graphs are asked.
	Graph graph;
	const NodeId id = graph.add<LoopNode>();
	auto& loop = static_cast<LoopNode&>(graph.node(id));
	Evaluation evaluation{graph};

	REQUIRE_FALSE(mentions(collectIssues(graph, evaluation, GraphPath{}), "'count'"));

	Evaluation inner{loop.inner()};
	REQUIRE_FALSE(mentions(collectIssues(loop.inner(), inner, GraphPath{{id}}), "'continue'"));
}

TEST_CASE("an output is only a dead end once its node can actually run", "[validation]")
{
	// Not about defaults: this is the rest of collectIssues, pinned so the move out of the pane is
	// known to have carried it faithfully. It is the one rule that consults the EVALUATION rather
	// than the definition — a node that cannot run yet has an unused output for a reason already
	// reported one line above, and saying both would be two rows for one problem.
	Graph graph;
	const NodeId blur = graph.add<example::BlurNode>(2, 1.5f);

	// RUN it first, so what separates the two halves is readiness alone and not "has anything
	// happened yet": an unfed blur is not ready however many times the graph is run.
	Evaluation unfed{graph};
	SerialScheduler{}.run(graph, unfed);
	REQUIRE_FALSE(mentions(collectIssues(graph, unfed, GraphPath{}), "is unused"));

	const NodeId source = graph.add<ImageSource>();
	REQUIRE(graph.connect(source, 0, blur, 0) == Connection::Ok);
	Evaluation fed{graph};
	SerialScheduler{}.run(graph, fed);
	const std::vector<Issue> issues = collectIssues(graph, fed, GraphPath{});
	REQUIRE(mentions(issues, "Blur")); // ...and now the blur's own output has nowhere to go
	REQUIRE(mentions(issues, "output 'image' is unused"));
}

//=========================================================================
// Failures (M14 slice 6): what threw, where it threw
//=========================================================================

namespace flowview::test::validation
{
	// An int source, and an int passthrough that always throws.
	struct IntSource : Node
	{
		PortId out;
		IntSource()
			: Node("IntSource")
		{
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<IntSource>(*this); }
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).set(1); }
	};

	struct Boom : Node
	{
		PortId in, out;
		Boom()
			: Node("Boom")
		{
			in = addInput<int>("x");
			out = addOutput<int>("y");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Boom>(*this); }
		void compute(NodeEvaluation&) const override { throw std::runtime_error("boom"); }
	};

	struct MakeInts : Node
	{
		PortId out;
		MakeInts()
			: Node("MakeInts")
		{
			out = addOutput<std::vector<int>>("items");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<MakeInts>(*this); }
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).set(std::vector<int>{1, 2, 3}); }
	};

	// x -> Boom -> y inside `group`'s interior; answers the Boom.
	static NodeId boomInside(Graph& parent, NodeId group, Graph& inner)
	{
		const PortId in = inner.boundaryInputNode().addBoundary<int>("x");
		const PortId out = inner.boundaryOutputNode().addBoundary<int>("y");
		const NodeId boom = inner.add<Boom>();
		REQUIRE(inner.connect(PortAddress{inner.boundaryInputNode().id(), in}, PortAddress{boom, inner.node(boom).input(0).id()}) == Connection::Ok);
		REQUIRE(inner.connect(PortAddress{boom, inner.node(boom).output(0).id()}, PortAddress{inner.boundaryOutputNode().id(), out}) == Connection::Ok);
		edit::syncGroupPorts(parent, group);
		return boom;
	}

	// Run a clone of `document` — which throws — and publish what it left, as the runner does.
	static PublishedEvaluation runAndPublish(const Graph& document, Evaluation& working, Scheduler& scheduler)
	{
		auto clone = std::make_shared<const Graph>(document.clone());
		REQUIRE_THROWS_AS(scheduler.run(*clone, working), std::runtime_error);
		return PublishedEvaluation{clone, working};
	}
} // namespace flowview::test::validation

using namespace flowview::test::validation;

TEST_CASE("a node that threw is an Error row that locates it", "[validation][failure]")
{
	Graph document;
	const NodeId source = document.add<IntSource>();
	const NodeId boom = document.add<Boom>();
	REQUIRE(document.connect(source, 0, boom, 0) == Connection::Ok);
	Evaluation working{document};
	SerialScheduler scheduler;
	const PublishedEvaluation published = runAndPublish(document, working, scheduler);

	const std::vector<Issue> rows = collectFailures(published.evaluation(), GraphPath{});
	REQUIRE(rows.size() == 1);
	REQUIRE(rows[0].severity == Issue::Severity::Error);
	REQUIRE(rows[0].node == boom);
	REQUIRE(rows[0].message.find("failed: boom") != std::string::npos);

	// Nothing published, nothing to say.
	REQUIRE(collectFailures(PublishedEvaluation{}.evaluation(), GraphPath{}).empty());
}

TEST_CASE("a failure inside a group leads into it, and locates it once there", "[validation][failure][group]")
{
	// A failure one level down is exactly what the user has not descended to see, so it is listed from
	// the root — as a row that goes there.
	Graph document;
	const NodeId source = document.add<IntSource>();
	const NodeId group = document.add<InlineGroupNode>();
	const NodeId boom = boomInside(document, group, static_cast<InlineGroupNode&>(document.node(group)).inner());
	REQUIRE(document.connect(source, 0, group, 0) == Connection::Ok);
	Evaluation working{document};
	SerialScheduler scheduler;
	const PublishedEvaluation published = runAndPublish(document, working, scheduler);

	const GraphPath inside{PathStep{group, 0}};
	const std::vector<Issue> fromRoot = collectFailures(published.evaluation(), GraphPath{});
	REQUIRE(fromRoot.size() == 1);
	REQUIRE(fromRoot[0].navigateTo == inside);

	const std::vector<Issue> fromInside = collectFailures(published.evaluation(), inside);
	REQUIRE(fromInside.size() == 1);
	REQUIRE(fromInside[0].node == boom);
	REQUIRE(fromInside[0].navigateTo.empty());
}

TEST_CASE("one node failing in several map elements is one row", "[validation][failure][map]")
{
	// Under the pool every element runs — they are independent branches, and only a thrower's
	// successors are skipped — so all three throw and all three are recorded.
	registerPortType<int>("Int");
	registerPortType<std::vector<int>>("ListOfInt");
	lain::testing::ThreadPool pool;
	Graph document;
	const NodeId list = document.add<MakeInts>();
	const NodeId map = document.add<MapNode>();
	boomInside(document, map, static_cast<MapNode&>(document.node(map)).inner());
	REQUIRE(document.connect(list, 0, map, 0) == Connection::Ok);
	Evaluation working{document};
	ParallelScheduler scheduler;
	const PublishedEvaluation published = runAndPublish(document, working, scheduler);
	REQUIRE(published.evaluation().childCount(map) == 3);

	const std::vector<Issue> rows = collectFailures(published.evaluation(), GraphPath{});
	REQUIRE(rows.size() == 1);
	REQUIRE(rows[0].message.find("and in 2 more element(s)") != std::string::npos);
	REQUIRE(rows[0].navigateTo == GraphPath{PathStep{map, 0}}); // the first, in element order
}
