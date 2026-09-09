// The Issues panel's rules, apart from the panel (src/validation.cpp). Driver-free: collectIssues is
// a pure function of a definition plus its runtime state, which is the whole reason it was split out
// of the pane — while it lived there, nothing in the suite could reach it, and it spent two
// milestones telling every Blur, Gate, Select and Loop that a defaulted input it seeds ITSELF was a
// missing connection.

#include "validation.h"

#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/example/blurnode.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace lain::flow;
using flowview::collectIssues;
using flowview::GraphPath;
using flowview::Issue;

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
