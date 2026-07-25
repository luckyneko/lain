// UndoStack — the flowview graph-document history. Driver-free: it operates on data::Value states
// (here, plain integers stand in for graph snapshots), so it exercises the real push/undo/redo/
// truncate/dedup/cap logic without a graph or a gui.

#include "undo.h"

#include <lain/data/value.h>

#include <catch2/catch_test_macros.hpp>

using flowview::UndoStack;
using lain::data::Value;

// A distinct state, labelled by an int (stands in for a serialized graph document).
static Value state(int n)
{
	return Value(n);
}

TEST_CASE("a fresh stack has no baseline and nothing to undo/redo", "[undo]")
{
	UndoStack undo;
	REQUIRE_FALSE(undo.hasBaseline());
	REQUIRE_FALSE(undo.canUndo());
	REQUIRE_FALSE(undo.canRedo());
}

TEST_CASE("reset seeds the baseline as the sole current state", "[undo]")
{
	UndoStack undo;
	undo.reset(state(0));
	REQUIRE(undo.hasBaseline());
	REQUIRE_FALSE(undo.canUndo()); // baseline is the current state — nowhere back
	REQUIRE_FALSE(undo.canRedo());
}

TEST_CASE("push then undo/redo walk the history", "[undo]")
{
	UndoStack undo;
	undo.reset(state(0));
	undo.push(state(1));
	undo.push(state(2));

	REQUIRE(undo.canUndo());
	REQUIRE_FALSE(undo.canRedo());

	REQUIRE(undo.undo() == state(1));
	REQUIRE(undo.undo() == state(0));
	REQUIRE_FALSE(undo.canUndo()); // back at the baseline
	REQUIRE(undo.canRedo());

	REQUIRE(undo.redo() == state(1));
	REQUIRE(undo.redo() == state(2));
	REQUIRE_FALSE(undo.canRedo()); // forward at the tip
}

TEST_CASE("push ignores a state equal to the current one", "[undo]")
{
	// A bound-value-only edit produces an unchanged document; the stack must record nothing, so undo
	// doesn't step through no-op entries.
	UndoStack undo;
	undo.reset(state(0));
	undo.push(state(0)); // identical -> ignored
	undo.push(state(0)); // still identical
	REQUIRE_FALSE(undo.canUndo());

	undo.push(state(1)); // a real change
	undo.push(state(1)); // no further change -> ignored
	REQUIRE(undo.canUndo());
	REQUIRE(undo.undo() == state(0));
	REQUIRE_FALSE(undo.canUndo()); // only one real step existed
}

TEST_CASE("a push after an undo discards the redo tail", "[undo]")
{
	UndoStack undo;
	undo.reset(state(0));
	undo.push(state(1));
	undo.push(state(2));

	REQUIRE(undo.undo() == state(1)); // now at 1, with 2 ahead
	REQUIRE(undo.canRedo());

	undo.push(state(3)); // branch off 1 -> the 2 tail is gone
	REQUIRE_FALSE(undo.canRedo());
	REQUIRE(undo.undo() == state(1));
	REQUIRE(undo.redo() == state(3)); // 3 replaced 2 as the forward state
}

TEST_CASE("the first push with no baseline becomes the baseline", "[undo]")
{
	UndoStack undo;
	undo.push(state(7)); // no reset first
	REQUIRE(undo.hasBaseline());
	REQUIRE_FALSE(undo.canUndo());
}

TEST_CASE("history is bounded to maxDepth, dropping the oldest", "[undo]")
{
	UndoStack undo;
	undo.reset(state(0));
	// Push well past the cap with distinct states; the oldest fall off the back.
	const int pushes = static_cast<int>(UndoStack::maxDepth) + 5;
	for (int i = 1; i <= pushes; ++i)
		undo.push(state(i));

	// The stack holds maxDepth states, so it can undo maxDepth-1 times before hitting the (new) oldest.
	int steps = 0;
	while (undo.canUndo())
	{
		undo.undo();
		++steps;
	}
	REQUIRE(steps == static_cast<int>(UndoStack::maxDepth) - 1);
}
