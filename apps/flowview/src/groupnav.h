#pragma once

// Navigating INTO group nodes. flowview shows one graph at a time — the ACTIVE GRAPH — identified
// by a path of group nodes descended from the root. Every pane reads that one graph, so descending
// retargets the canvas, Inspector, Preview and Issues together (ADR-0009/0010's "the top-level
// Graph is the outermost group", made literal in the UI).
//
// Both group kinds are navigable: you can look inside a LINKED group and inspect its live
// intermediates, but its inner graph is read-only in place — changing it changes every linked group
// built from that template, so that is an explicit "Edit Template…" act, not a side effect of
// clicking in.

#include <lain/flow/serialize/loadresult.h> // EditorTree — layout mirrors the nesting
#include <lain/flow/types.h>

#include <cstddef>
#include <string>
#include <vector>

namespace lain::flow
{
	class Graph;
	class LinkedGroupNode;
} // namespace lain::flow

namespace flowview
{
	// The path from the root to the active graph: the group node descended through at each level.
	// Empty means the root graph itself.
	using GraphPath = std::vector<lain::flow::NodeId>;

	// Resolve `path` against `root`, returning the graph it names. TOLERANT: a step that is missing
	// or no longer a group stops the walk and returns the deepest graph that did resolve, so a stale
	// path degrades to an ancestor rather than dangling. Pass `path` by reference to have it
	// truncated to what actually resolved.
	lain::flow::Graph& resolvePath(lain::flow::Graph& root, GraphPath& path);

	// One breadcrumb entry: the label to show and the path depth clicking it navigates to.
	struct Crumb
	{
		std::string label;
		std::size_t depth; // path length once this crumb is chosen (0 = root)
	};

	// The breadcrumb for `path`: "root" plus each descended group's display name.
	std::vector<Crumb> breadcrumb(lain::flow::Graph& root, const GraphPath& path);

	// Whether the graph at `path` may be EDITED. False inside a linked group (and inside anything
	// nested within one): its recipe belongs to its template.
	bool editableAt(lain::flow::Graph& root, const GraphPath& path);

	// The enclosing LINKED group, or nullptr if there is none — what "Edit Template…" acts on, and
	// what the canvas names when it explains why editing is off. Returns the OUTERMOST linked group on
	// the path: that is the one whose template owns everything below it, and the only one whose stored
	// `source` is relative to the document currently open.
	//
	// Returns the NODE, not its id, deliberately: the group can sit at any depth (inside an inline
	// group, say), so an id alone would send the caller looking in the wrong graph for it.
	lain::flow::LinkedGroupNode* enclosingLinkedGroup(lain::flow::Graph& root, const GraphPath& path);

	// Re-derive the outer ports of every group along `path` from its own inner boundary, and report
	// whether anything moved. A group's ports mirror its interior, and that interior can be changed
	// from several places (the Interface panel's ± and renames while descended, the canvas ±, an undo
	// restore), so rather than enumerating those paths the host reconciles the ones it is inside.
	// syncGroupPorts is idempotent and proportional to the pin count, so a no-op pass costs nothing.
	bool syncPathGroups(lain::flow::Graph& root, const GraphPath& path);

	// The active path expressed as ORDINALS into each level's node enumeration, and back again. A
	// document restore (undo/redo) rebuilds the graph with FRESH NodeIds, so a path of ids is
	// meaningless afterwards — but the ordinal is stable across a structure-preserving edit, because
	// both the old and new graphs enumerate in insertion order. This is the same trick the canvas
	// selection uses, applied one level at a time so it works at any depth.
	std::vector<std::size_t> pathOrdinals(lain::flow::Graph& root, const GraphPath& path);
	// Tolerant, like resolvePath: an ordinal that is out of range, or names a node that is no longer a
	// group, ends the walk — so a restore that really did remove the group lands on its parent.
	GraphPath pathFromOrdinals(lain::flow::Graph& root, const std::vector<std::size_t>& ordinals);

	// How many linked groups anywhere in `root` (at any depth) are built from `source` — the blast
	// radius of editing that template, which the Edit affordance states before you commit to it.
	int countLinkedInstances(lain::flow::Graph& root, const std::string& source);

	// The layout subtree for `path`, creating empty levels as needed — so the canvas reads and writes
	// positions for the level it is actually showing, and other levels keep theirs.
	lain::flow::serialize::EditorTree& layoutAt(lain::flow::serialize::EditorTree& root, const GraphPath& path);
	const lain::flow::serialize::EditorTree* findLayoutAt(const lain::flow::serialize::EditorTree& root, const GraphPath& path);
} // namespace flowview
