#pragma once

// The app half of the group-authoring gestures: Group Selected, Ungroup, Save as Template and Make
// Local. flow::edit owns the graph surgery (it is testable without a window and knows nothing about
// canvases or files); what is left here is the three things only the host can do —
//
//   1. read the CANVAS: what is selected, and where the nodes actually sit right now;
//   2. carry the LAYOUT across the change, so nodes do not scatter into default columns;
//   3. touch the FILE SYSTEM and the template cache, which flow core deliberately cannot.
//
// Each returns whether the document changed, so the caller marks it dirty and re-runs exactly as it
// does for any other edit. A refusal is reported through AppContext's transient-issue channel rather
// than swallowed: a gesture that silently does nothing reads as a bug.

#include "groupnav.h" // GraphPath — the level a gesture acts at

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	struct AppContext;

	// Whether any of these gestures is available right now, so a menu can grey out rather than fail
	// on click. Each asks a different question of the canvas selection, and they are separate
	// functions because a single "canGroup"-style flag would have to pick one of them to mean.
	bool canGroupSelection(const AppContext& ctx, const lain::flow::Graph& activeGraph);
	// Exactly one INLINE group selected (Ungroup, Save as Template).
	bool canUngroupSelection(const AppContext& ctx, const lain::flow::Graph& activeGraph);
	// Exactly one LINKED group selected (Make Local).
	bool canMakeLocalSelection(const AppContext& ctx, const lain::flow::Graph& activeGraph);

	// Move the canvas selection into a new inline group, and put that group where the selection was.
	// The moved nodes' saved positions travel down into the group's layout subtree — they keep their
	// NodeIds across the move, so the layout is re-keyed by nothing at all.
	bool groupSelection(AppContext& ctx, const lain::flow::Graph& activeGraph);

	// Splice the selected inline group's interior back into this level, landing the lifted nodes
	// around where the group itself sat rather than at whatever coordinates they had inside it.
	bool ungroupSelection(AppContext& ctx, const lain::flow::Graph& activeGraph);

	// Write the selected inline group's interior out as its own template document, then re-point the
	// group at it as a LINK — so every other instance of that file, now and later, shares this one
	// definition. Prompts for the path.
	bool saveAsTemplate(AppContext& ctx, const lain::flow::Graph& activeGraph);

	// The reverse: embed the selected linked group's template into this document as an inline body,
	// so it can be edited here without touching the file every other instance is built from. Reads
	// the template from disk, which is the honest meaning of "make a local copy of it".
	bool makeLocal(AppContext& ctx, const lain::flow::Graph& activeGraph);
} // namespace flowview
