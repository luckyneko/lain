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

#include "runpolicy.h"

#include <catch2/catch_test_macros.hpp>

using flowview::RunRequests;
using flowview::RunTrigger;

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
	requests.started();
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
		requests.started();
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
		requests.started();
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
