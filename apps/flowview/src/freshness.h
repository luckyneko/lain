#pragma once

// FRESHNESS (M14 slice 6, ADR-0025): whether a node's SHOWN value reflects the document. The panes
// read a published evaluation, which can be a stage, a run or — under Manual — many edits behind the
// document; this is how they say so.
//
// The rule is not this file's. What is stale is flow's stale closure (flow::StaleClosure), asked of
// the DOCUMENT against the published evaluation; what failed is the run's own record
// (Evaluation::failure). This file only walks to the level on screen, carrying across each level
// whether its boundary is being handed new values — the part of "anything upstream is stale" that
// reaches INTO a group — and turns the two answers into one state per node.

#include "groupnav.h" // GraphPath

#include <lain/flow/types.h>

#include <cstddef>
#include <map>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

namespace flowview
{
	// Declared with no explicit underlying type, so a header that only draws one can forward-declare
	// it (`enum class Freshness;`).
	//
	// Queued and Computing join with M14 slice 7, whose per-step hook is what knows them. Every
	// switch over this is exhaustive, so -Wswitch is what makes that slice draw them.
	enum class Freshness
	{
		Current, // the shown value reflects the document. (Just updated is Current plus a highlight.)
		Stale,	 // it does not — or there is no shown value at all
		Failed,	 // its last compute threw — or, for a group, map or loop, something inside it did
	};

	// Every node of one level, and its freshness.
	struct LevelFreshness
	{
		std::map<lain::flow::NodeId, Freshness> nodes;

		// A node missing from the map is Stale: it was added after this was worked out (this frame's
		// edit), so nothing can have been computed for it yet.
		Freshness of(lain::flow::NodeId id) const;

		// How many of this level's nodes are in `state` — what the status bar summarises.
		std::size_t count(Freshness state) const;
	};

	// The freshness of every node on the level `path` names in `document`, against `published`.
	//
	// The walk truncates exactly as resolvePath does, so it describes the level the panes were given.
	// Where the published evaluation has no child for a step (nothing has run in there yet), that level
	// is compared against NOTHING — every node Stale. resolveEvaluation stops at the ancestor instead,
	// which would give the same answer here only by accident (an ancestor's records are of another
	// lineage, so none of them pair); saying "nothing" is what the level actually has.
	LevelFreshness levelFreshness(const lain::flow::Graph& document, const lain::flow::Evaluation& published,
								  const GraphPath& path);

	// --- drawing -------------------------------------------------------------
	// The mark for a state, centred at (x, y) in screen space into the current window's draw list:
	// nothing for Current, a hollow ring for Stale, a filled red disc with a "!" for Failed. Shapes,
	// so it reads without colour — the default font has no symbols past U+00FF to use instead. The
	// one drawing of a state, shared by the canvas title glyph and the thumbnail badge.
	void drawFreshnessMark(float x, float y, float radius, Freshness state);

	// A badge over the LAST ITEM drawn — a thumbnail — when its value is not Current, with a tooltip
	// saying what the badge means while the item is hovered. Submits no item, so the caller can still
	// ask IsItemClicked() about the thumbnail afterwards.
	void drawFreshnessBadge(Freshness state);
} // namespace flowview
