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
#include <lain/math/types.h> // Vec2f — canvas positions, for the layout migration below

#include <cstddef>
#include <string>
#include <vector>

namespace lain::flow
{
	class Evaluation;
	class Graph;
	class LinkedGroupNode;
} // namespace lain::flow

namespace flowview
{
	// One step down the path: which graph-containing node was descended through, and WHICH
	// EVALUATION OF IT. A group has exactly one, so its element is always 0; a MAP has one per
	// element, and the element is what the breadcrumb's stepper picks (ADR-0014).
	//
	// This is the `{NodeId, index}` step ADR-0012 called an EvalPath and CONTEXT.md has been holding
	// a note for: until maps existed a group's graph walk and evaluation walk were the same sequence,
	// so a bare NodeId said everything. A map is what makes them differ.
	struct PathStep
	{
		lain::flow::NodeId node;
		std::size_t element = 0;

		bool operator==(const PathStep& other) const { return node == other.node && element == other.element; }
		bool operator!=(const PathStep& other) const { return !(*this == other); }
		bool operator<(const PathStep& other) const
		{
			if (node != other.node)
				return node < other.node;
			return element < other.element;
		}
	};

	// The path from the root to the active graph: the step descended through at each level.
	// Empty means the root graph itself.
	using GraphPath = std::vector<PathStep>;

	// Resolve `path` against `root`, returning the graph it names — for READING (navigation, drawing,
	// previews, validation). TOLERANT: a step that is missing or no longer a group stops the walk and
	// returns the deepest graph that did resolve, so a stale path degrades to an ancestor rather than
	// dangling. Pass `path` by reference to have it truncated to what actually resolved.
	//
	// Const, because a contained graph is a definition and may be shared (ADR-0013). Descending into a
	// LINKED group is deliberately still allowed — inspecting a template's live intermediates is the
	// point of being able to look inside one; what you cannot do is edit it, which is what the
	// separate resolution below expresses.
	const lain::flow::Graph& resolvePath(const lain::flow::Graph& root, GraphPath& path);

	// The same walk, for EDITING: the graph `path` names, or nullptr when any step of it crosses into
	// a linked group — whose recipe belongs to its template, not to this document.
	//
	// Two functions rather than one plus an `editableAt` check, because "may I edit this?" and "hand
	// me something I can edit" are the same question, and answering it twice is how a pane forgets
	// (M5's bugs three and ten: the Interface pane's ± and the Inspector's params were silently lost
	// on save inside a linked group). Here a pane that has no mutable graph cannot mutate one.
	lain::flow::Graph* resolveEditable(lain::flow::Graph& root, const GraphPath& path);

	// The matching RUNTIME state for that graph: an Evaluation is a tree with one child per group
	// node, so the same path walks it. Tolerant in the same way — a step with no child Evaluation
	// stops the walk — so the two resolutions agree even mid-edit. Call it with the path
	// resolvePath already truncated, and the panes get a definition and its values in step.
	lain::flow::Evaluation& resolveEvaluation(lain::flow::Evaluation& root, const GraphPath& path);

	// One breadcrumb entry: the label to show and the path depth clicking it navigates to.
	struct Crumb
	{
		std::string label;
		std::size_t depth; // path length once this crumb is chosen (0 = root)
	};

	// The breadcrumb for `path`: "root" plus each descended group's display name.
	std::vector<Crumb> breadcrumb(const lain::flow::Graph& root, const GraphPath& path);

	// The enclosing LINKED group, or nullptr if there is none — what "Edit Template…" acts on, and
	// what the canvas names when it explains why editing is off. Returns the OUTERMOST linked group on
	// the path: that is the one whose template owns everything below it, and the only one whose stored
	// `source` is relative to the document currently open.
	//
	// Returns the NODE, not its id, deliberately: the group can sit at any depth (inside an inline
	// group, say), so an id alone would send the caller looking in the wrong graph for it.
	const lain::flow::LinkedGroupNode* enclosingLinkedGroup(const lain::flow::Graph& root, const GraphPath& path);

	// Re-derive the outer ports of every group along `path` from its own inner boundary, and report
	// whether anything moved. A group's ports mirror its interior, and that interior can be changed
	// from several places (the Interface panel's ± and renames while descended, the canvas ±, an undo
	// restore), so rather than enumerating those paths the host reconciles the ones it is inside.
	// syncGroupPorts is idempotent and proportional to the pin count, so a no-op pass costs nothing.
	bool syncPathGroups(lain::flow::Graph& root, const GraphPath& path);

	// How many linked groups anywhere in `root` (at any depth) are built from `source` — the blast
	// radius of editing that template, which the Edit affordance states before you commit to it.
	int countLinkedInstances(const lain::flow::Graph& root, const std::string& source);

	// Whether `root` contains any linked group at all (at any depth) — what Reload Linked Groups has
	// to work on. Separate from countLinkedInstances rather than an "empty source means any" flag on
	// it: the two questions are different, and one function answering both by argument value is how a
	// caller ends up asking the wrong one.
	bool hasLinkedGroups(const lain::flow::Graph& root);

	// The layout subtree for `path`, creating empty levels as needed — so the canvas reads and writes
	// positions for the level it is actually showing, and other levels keep theirs.
	lain::flow::serialize::EditorTree& layoutAt(lain::flow::serialize::EditorTree& root, const GraphPath& path);
	const lain::flow::serialize::EditorTree* findLayoutAt(const lain::flow::serialize::EditorTree& root, const GraphPath& path);

	// --- Layout migration: keeping positions when nodes change level -----------------------------
	// The pure-data half of the group gestures, here rather than beside them because this is where
	// the layout tree is understood — and because a mistake in it is SILENT: a node whose entry did
	// not travel simply appears in a default column, which reads as a bug in something else entirely.

	// Move the entries for `moved` out of `parent` and into the subtree for `group` — Group Selected's
	// layout half. The nodes keep their ids across the move (Graph::extract), so the entries move
	// under the same keys; a node with no entry is skipped rather than given a placeholder. A moved
	// node that is ITSELF a group takes its whole subtree along, since nesting is arbitrarily deep.
	void descendLayout(lain::flow::serialize::EditorTree& parent, lain::flow::NodeId group,
					   const std::vector<lain::flow::NodeId>& moved);

	// The reverse, for Ungroup: lift the subtrees of `moved` out of `group`'s level into `parent`, then
	// drop what is left of that level. Positions are NOT moved here — those have to be translated onto
	// where the group sat, which is liftedPositions' job and which their nested contents do not need.
	void ascendLayout(lain::flow::serialize::EditorTree& parent, lain::flow::NodeId group,
					  const std::vector<lain::flow::NodeId>& moved);

	// Where each of `moved` should land when a group sitting at `groupPos` is opened up: their
	// arrangement from inside the group, translated so its centre of mass falls on the group's own
	// position. Verbatim inner coordinates would not do — an inner graph's grid space starts near the
	// origin, so the lifted nodes would fly off to a corner of a canvas the user is not looking at.
	// A node with no recorded inner position falls back to `groupPos`.
	std::vector<lain::math::Vec2f> liftedPositions(const lain::flow::serialize::EditorData& innerLayout,
												   const std::vector<lain::flow::NodeId>& moved,
												   lain::math::Vec2f groupPos);
} // namespace flowview
