// The run triggers (M14 slice 5, ADR-0025): when a change becomes a run. FlowviewApp::pumpRun asks
// RunRequests::due every frame and starts (or supersedes) exactly when it answers yes, so these cases
// are the production rule — the frame loop around it is the only part not here.
//
// What each case pins:
//   * Live runs every change, mid-gesture included (today's behaviour, minus the blocking);
//   * On commit runs a change only once the gesture has ended — a drag is one run, on release;
//   * Manual never runs a change, and so never cancels a run in flight;
//   * an explicit Run is due under every trigger;
//   * a started run answers both asks, and Stop forgets both;
//   * a change owed under Manual is still owed after a switch to Live.
//
// And a RUN SELECTION (M14 slice 8), which reaches only part of the closure:
//   * it is due under every trigger, and carries its targets;
//   * a Run, or a change that is due, asks for the whole closure — which covers any selection;
//   * a partial start answers only the selection, so a change it did not cover still asks;
//   * Stop forgets it, a newer one replaces it, and an empty one asks for nothing;
//   * a selection inside a group means the root-level group the path descends through.

#include "runpolicy.h"

#include <lain/flow/types.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using flowview::RunRequests;
using flowview::RunScope;
using flowview::RunTrigger;
using flowview::selectionTargets;
using lain::flow::EvalPath;
using lain::flow::EvalStep;
using lain::flow::NodeId;

TEST_CASE("Live runs every change, mid-gesture included", "[runpolicy]")
{
	RunRequests requests;
	REQUIRE_FALSE(requests.due(RunTrigger::Live, true)); // nothing asked yet

	requests.changed();
	REQUIRE(requests.due(RunTrigger::Live, false)); // a drag frame
	REQUIRE(requests.due(RunTrigger::Live, true));
}

TEST_CASE("On commit runs a change once the gesture has ended", "[runpolicy]")
{
	RunRequests requests;

	// Every frame of a drag asks — and none of them is due while the drag is held.
	for (int frame = 0; frame < 5; ++frame)
	{
		requests.changed();
		REQUIRE_FALSE(requests.due(RunTrigger::OnCommit, false));
	}

	// Released: the whole drag is ONE run.
	REQUIRE(requests.due(RunTrigger::OnCommit, true));
	requests.started(RunScope{});
	REQUIRE_FALSE(requests.due(RunTrigger::OnCommit, true));

	// A click is a gesture that ends in the frame it happens.
	requests.changed();
	REQUIRE(requests.due(RunTrigger::OnCommit, true));
}

TEST_CASE("Manual never runs a change, whatever the gesture", "[runpolicy]")
{
	RunRequests requests;
	requests.changed();
	REQUIRE_FALSE(requests.due(RunTrigger::Manual, false));
	REQUIRE_FALSE(requests.due(RunTrigger::Manual, true));
}

TEST_CASE("an explicit Run is due under every trigger", "[runpolicy]")
{
	for (const RunTrigger trigger : {RunTrigger::Live, RunTrigger::OnCommit, RunTrigger::Manual})
	{
		RunRequests requests;
		requests.runNow();
		// Mid-gesture too: Run is a menu item or a shortcut, and a shortcut can land while a widget
		// elsewhere is still active.
		REQUIRE(requests.due(trigger, false));
		REQUIRE(requests.due(trigger, true));
	}
}

TEST_CASE("a started run answers both asks, and Stop forgets both", "[runpolicy]")
{
	SECTION("started")
	{
		RunRequests requests;
		requests.changed();
		requests.runNow();
		requests.started(RunScope{});
		REQUIRE_FALSE(requests.due(RunTrigger::Live, true));
		REQUIRE_FALSE(requests.due(RunTrigger::Manual, true));
	}

	SECTION("stopped")
	{
		RunRequests requests;
		requests.changed();
		requests.runNow();
		requests.clear();
		REQUIRE_FALSE(requests.due(RunTrigger::Live, true));
		REQUIRE_FALSE(requests.due(RunTrigger::Manual, true));
	}

	SECTION("a change made after the start asks again")
	{
		RunRequests requests;
		requests.changed();
		requests.started(RunScope{});
		requests.changed(); // an edit while that run is in flight
		REQUIRE(requests.due(RunTrigger::Live, true));
	}
}

TEST_CASE("a change owed under Manual is still owed after a switch to Live", "[runpolicy]")
{
	RunRequests requests;
	requests.changed();
	REQUIRE_FALSE(requests.due(RunTrigger::Manual, true)); // asked every frame while Manual — not lost by it

	// The trigger is only an input to due(), so switching it back is enough.
	REQUIRE(requests.due(RunTrigger::OnCommit, true));
	REQUIRE(requests.due(RunTrigger::Live, true));
}

//=========================================================================
// Run Selection
//=========================================================================

TEST_CASE("a Run Selection is due under every trigger and carries its targets", "[runpolicy]")
{
	const std::vector<NodeId> targets{NodeId::generate(), NodeId::generate()};
	for (const RunTrigger trigger : {RunTrigger::Live, RunTrigger::OnCommit, RunTrigger::Manual})
	{
		RunRequests requests;
		requests.runSelection(targets);
		REQUIRE(requests.due(trigger, false)); // a shortcut can land mid-gesture, as Run's can
		const RunScope scope = requests.scope(trigger, true);
		REQUIRE_FALSE(scope.whole());
		REQUIRE(*scope.targets == targets);
	}
}

TEST_CASE("a Run or a due change asks for the whole closure, covering a selection", "[runpolicy]")
{
	const std::vector<NodeId> targets{NodeId::generate()};

	SECTION("Run")
	{
		RunRequests requests;
		requests.runSelection(targets);
		requests.runNow();
		REQUIRE(requests.scope(RunTrigger::Manual, true).whole());
		requests.started(RunScope{});
		REQUIRE_FALSE(requests.due(RunTrigger::Manual, true)); // the whole run answered the selection too
	}

	SECTION("a change, where it is due")
	{
		RunRequests requests;
		requests.runSelection(targets);
		requests.changed();
		REQUIRE(requests.scope(RunTrigger::Live, false).whole());
		REQUIRE(requests.scope(RunTrigger::OnCommit, true).whole());
		// ... and not where it is not: mid-drag under On commit, and always under Manual, the change
		// waits and the selection runs.
		REQUIRE_FALSE(requests.scope(RunTrigger::OnCommit, false).whole());
		REQUIRE_FALSE(requests.scope(RunTrigger::Manual, true).whole());
	}
}

TEST_CASE("a partial start answers only the selection, so a change still asks", "[runpolicy]")
{
	RunRequests requests;
	requests.changed(); // owed under Manual
	requests.runSelection({NodeId::generate()});

	const RunScope scope = requests.scope(RunTrigger::Manual, true);
	REQUIRE_FALSE(scope.whole());
	requests.started(scope);

	// Manual: the change waits, as every change does there — and the selection has been answered.
	REQUIRE_FALSE(requests.due(RunTrigger::Manual, true));
	// Live: the change is due, and whole — which is what supersedes the partial run in flight.
	REQUIRE(requests.due(RunTrigger::Live, true));
	REQUIRE(requests.scope(RunTrigger::Live, true).whole());
}

TEST_CASE("Stop forgets a selection, a newer one replaces it, and an empty one asks nothing", "[runpolicy]")
{
	SECTION("stopped")
	{
		RunRequests requests;
		requests.runSelection({NodeId::generate()});
		requests.clear();
		REQUIRE_FALSE(requests.due(RunTrigger::Manual, true));
	}

	SECTION("replaced")
	{
		const NodeId newer = NodeId::generate();
		RunRequests requests;
		requests.runSelection({NodeId::generate()});
		requests.runSelection({newer});
		REQUIRE(*requests.scope(RunTrigger::Manual, true).targets == std::vector<NodeId>{newer});
	}

	SECTION("empty")
	{
		RunRequests requests;
		requests.runSelection({});
		REQUIRE_FALSE(requests.due(RunTrigger::Manual, true));
	}
}

TEST_CASE("a selection inside a group means the root-level group it is in", "[runpolicy]")
{
	const NodeId a = NodeId::generate();
	const NodeId b = NodeId::generate();
	const NodeId outer = NodeId::generate();
	const NodeId inner = NodeId::generate();

	// At the root, the selection itself.
	REQUIRE(selectionTargets(EvalPath{}, {a, b}) == std::vector<NodeId>{a, b});

	// Two levels down — and on a map's third element — the node the path leaves the root through,
	// which runs whole.
	const EvalPath nested{EvalStep{outer, 2}, EvalStep{inner, 0}};
	REQUIRE(selectionTargets(nested, {a, b}) == std::vector<NodeId>{outer});

	// Nothing selected is nothing to run, at any depth.
	REQUIRE(selectionTargets(EvalPath{}, {}).empty());
	REQUIRE(selectionTargets(nested, {}).empty());
}
