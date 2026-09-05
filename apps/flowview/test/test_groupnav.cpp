// groupnav — flowview's model of "which graph am I looking at". Driver-free: it operates on a plain
// Graph and an EditorTree, so the navigation logic is tested without a window. The *frame ordering*
// around it (a navigation requested during a draw must be consumed before the next one resolves the
// path) lives in MainWindow and still needs a live driver; what is pinned here is everything else.

#include "groupnav.h"

#include <lain/data/value.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <utility> // std::move (adopting a link's interior)
#include <vector>

using namespace lain::flow;
using flowview::ascendLayout;
using flowview::breadcrumb;
using flowview::Crumb;
using flowview::descendLayout;
using flowview::enclosingLinkedGroup;
using flowview::findLayoutAt;
using flowview::GraphPath;
using flowview::hasLinkedGroups;
using flowview::layoutAt;
using flowview::liftedPositions;
using flowview::pathElementCounts;
using flowview::PathStep;
using flowview::resolveEditable;
using flowview::resolveEvaluation;
using flowview::resolvePath;
using flowview::syncPathGroups;

namespace
{
	// A group inside `parent`, named for legibility in breadcrumb assertions.
	template <typename T>
	NodeId addGroup(Graph& parent, const char* name)
	{
		const NodeId id = parent.add<T>();
		parent.node(id).setName(name);
		return id;
	}

	// A per-node editor blob, in the shape the canvas writes: an opaque object with x / y.
	lain::data::Value pos(double x, double y)
	{
		lain::data::Value value = lain::data::Value::object();
		value.set("x", lain::data::Value(x));
		value.set("y", lain::data::Value(y));
		return value;
	}

	// That blob read back, as a pair so a test can compare both coordinates in one assertion.
	std::pair<double, double> at(const serialize::EditorData& layout, NodeId id)
	{
		const lain::data::Value& value = layout.at(id);
		const lain::data::Value* x = value.find("x");
		const lain::data::Value* y = value.find("y");
		return {x ? x->asDouble().value_or(0.0) : 0.0, y ? y->asDouble().value_or(0.0) : 0.0};
	}
} // namespace

TEST_CASE("an empty path is the root graph", "[groupnav]")
{
	Graph root;
	GraphPath path;
	REQUIRE(&resolvePath(root, path) == &root);
	REQUIRE(path.empty());
}

TEST_CASE("a path resolves down through nested groups", "[groupnav]")
{
	Graph root;
	const NodeId outer = addGroup<InlineGroupNode>(root, "outer");
	Graph& mid = static_cast<InlineGroupNode&>(root.node(outer)).inner();
	const NodeId inner = addGroup<InlineGroupNode>(mid, "inner");
	Graph& deep = static_cast<InlineGroupNode&>(mid.node(inner)).inner();

	GraphPath path{{outer}, {inner}};
	REQUIRE(&resolvePath(root, path) == &deep);
	REQUIRE(path.size() == 2); // fully resolved, so nothing was truncated
}

TEST_CASE("a stale path degrades to the deepest level that still resolves", "[groupnav]")
{
	// The host holds a path across edits, so a group deleted from under it must not dangle.
	Graph root;
	const NodeId outer = addGroup<InlineGroupNode>(root, "outer");
	Graph& mid = static_cast<InlineGroupNode&>(root.node(outer)).inner();
	const NodeId inner = addGroup<InlineGroupNode>(mid, "inner");

	SECTION("a removed step truncates the path there")
	{
		REQUIRE(mid.removeNode(inner));
		GraphPath path{{outer}, {inner}};
		REQUIRE(&resolvePath(root, path) == &mid); // stopped at the parent
		REQUIRE(path.size() == 1);
	}

	SECTION("a step that is not a group truncates too")
	{
		GraphPath path{{outer}, {root.boundaryInputNode().id()}}; // a real node, but it contains no graph
		REQUIRE(&resolvePath(root, path) == &static_cast<InlineGroupNode&>(root.node(outer)).inner());
		REQUIRE(path.size() == 1);
	}

	SECTION("an id from another graph truncates immediately")
	{
		GraphPath path{{NodeId::generate()}};
		REQUIRE(&resolvePath(root, path) == &root);
		REQUIRE(path.empty());
	}
}

TEST_CASE("the breadcrumb names each level and where clicking it lands", "[groupnav]")
{
	Graph root;
	const NodeId outer = addGroup<InlineGroupNode>(root, "denoise");
	Graph& mid = static_cast<InlineGroupNode&>(root.node(outer)).inner();
	const NodeId inner = addGroup<InlineGroupNode>(mid, "sharpen");

	const std::vector<Crumb> crumbs = breadcrumb(root, GraphPath{{outer}, {inner}});
	REQUIRE(crumbs.size() == 3);
	REQUIRE(crumbs[0].label == "root");
	REQUIRE(crumbs[0].depth == 0); // clicking it returns to the root
	REQUIRE(crumbs[1].label == "denoise");
	REQUIRE(crumbs[1].depth == 1);
	REQUIRE(crumbs[2].label == "sharpen");
	REQUIRE(crumbs[2].depth == 2);
}

TEST_CASE("editing stops at a linked group, and stays off below it", "[groupnav]")
{
	// "May I edit here?" is now answered by whether there is a mutable graph to edit THROUGH: one
	// resolution for reading, another for writing that stops at a link (ADR-0013). A pane that forgets
	// to ask no longer silently writes into a template's interior — it has nothing to write into.
	Graph root;
	const NodeId inlineGroup = addGroup<InlineGroupNode>(root, "inline");
	const NodeId linked = addGroup<LinkedGroupNode>(root, "linked");

	// A link's interior arrives whole, from its template. Note there is no mutable accessor to build
	// it through — the interior is adopted, which is the type system stating the same rule.
	Graph interior;
	const NodeId nestedInLink = addGroup<InlineGroupNode>(interior, "nested");
	static_cast<LinkedGroupNode&>(root.node(linked)).adoptInterior(std::move(interior));

	REQUIRE(resolveEditable(root, GraphPath{}) == &root);							// the root document
	REQUIRE(resolveEditable(root, GraphPath{{inlineGroup}}) != nullptr);			// editable in place
	REQUIRE(resolveEditable(root, GraphPath{{linked}}) == nullptr);					// the link is not
	REQUIRE(resolveEditable(root, GraphPath{{linked}, {nestedInLink}}) == nullptr); // nor anything below it

	// Reading still descends all the way: looking inside a template is the point of being able to
	// navigate into one at all.
	GraphPath deep{{linked}, {nestedInLink}};
	REQUIRE(resolvePath(root, deep).nodeCount() == 2); // the nested group's own boundary pair
	REQUIRE(deep.size() == 2);						   // ... and nothing was truncated

	// "Edit Template..." acts on the OUTERMOST link — that is the template owning everything below,
	// and the only one whose stored `source` is relative to the open document.
	REQUIRE(enclosingLinkedGroup(root, GraphPath{{linked}, {nestedInLink}}) == &root.node(linked));
	REQUIRE(enclosingLinkedGroup(root, GraphPath{{inlineGroup}}) == nullptr);
}

TEST_CASE("a linked group nested inside an inline group is still found", "[groupnav]")
{
	// Why enclosingLinkedGroup returns the NODE: the link can sit at any depth, so an id alone would
	// send the caller looking for it in the root graph, where it does not live. That is exactly what
	// made "Edit Template..." a no-op for a nested link.
	Graph root;
	const NodeId outer = addGroup<InlineGroupNode>(root, "wrapper"); // an ordinary inline group...
	Graph& mid = static_cast<InlineGroupNode&>(root.node(outer)).inner();
	const NodeId linked = addGroup<LinkedGroupNode>(mid, "linked"); // ...holding the link

	// The trap, made explicit: the nested link is NOT in the root, so an id-based lookup there fails.
	// It used to fail silently and find a different node, because ids restarted per Graph; a uuid
	// makes it an honest miss (ADR-0011). Either way an id alone does not say which graph to look in,
	// which is why this returns the node itself.
	REQUIRE_FALSE(root.contains(linked));
	REQUIRE(mid.contains(linked));

	REQUIRE(enclosingLinkedGroup(root, GraphPath{{outer}, {linked}}) == &mid.node(linked));
	REQUIRE(resolveEditable(root, GraphPath{{outer}, {linked}}) == nullptr);
	REQUIRE(resolveEditable(root, GraphPath{{outer}}) == &mid); // the wrapper itself is still editable
}

TEST_CASE("layout is stored and found per level", "[groupnav]")
{
	// Why this matters: imnodes only knows the level on screen, so each level's positions have to be
	// kept apart and restored as it is shown. Node ids no longer repeat across levels (ADR-0011), so
	// the same id at two levels is now a stress case rather than a real document — but it is still the
	// sharpest way to show that the tree keys by LEVEL and the two levels do not share one map.
	lain::flow::serialize::EditorTree tree;
	const NodeId group = NodeId::generate();
	const NodeId node = group; // deliberately the SAME id, one level down

	lain::data::Value rootBlob = lain::data::Value::object();
	rootBlob.set("x", lain::data::Value(10.0));
	lain::data::Value innerBlob = lain::data::Value::object();
	innerBlob.set("x", lain::data::Value(99.0));

	layoutAt(tree, GraphPath{}).nodes[group] = rootBlob;
	layoutAt(tree, GraphPath{{group}}).nodes[node] = innerBlob;

	const auto* rootLevel = findLayoutAt(tree, GraphPath{});
	const auto* innerLevel = findLayoutAt(tree, GraphPath{{group}});
	REQUIRE(rootLevel != nullptr);
	REQUIRE(innerLevel != nullptr);
	REQUIRE(rootLevel->nodes.at(group).find("x")->asDouble() == 10.0);
	REQUIRE(innerLevel->nodes.at(node).find("x")->asDouble() == 99.0); // not the root's 10.0

	SECTION("an unvisited level simply has no stored layout")
	{
		REQUIRE(findLayoutAt(tree, GraphPath{{NodeId::generate()}}) == nullptr);
	}

	SECTION("layoutAt creates a level on demand, so a first visit can record into it")
	{
		const NodeId unvisited = NodeId::generate();
		layoutAt(tree, GraphPath{{unvisited}}).nodes[NodeId::generate()] = rootBlob;
		REQUIRE(findLayoutAt(tree, GraphPath{{unvisited}}) != nullptr);
	}
}

TEST_CASE("syncing the active path carries an inner interface out to the group's face", "[groupnav]")
{
	// The host's per-frame reconciliation: pins added to an inner boundary (from the Interface panel,
	// while descended) have to reach the group's own ports, and nothing else does that.
	Graph root;
	const NodeId group = addGroup<InlineGroupNode>(root, "group");
	Graph& inner = static_cast<InlineGroupNode&>(root.node(group)).inner();

	inner.boundaryInputNode().addBoundary<int>("in");
	REQUIRE(syncPathGroups(root, GraphPath{{group}}));
	REQUIRE(root.node(group).inputCount() == 1);

	// The second pin lands on the OTHER side, whose inner PortId collides with the first's.
	inner.boundaryOutputNode().addBoundary<int>("out");
	REQUIRE(syncPathGroups(root, GraphPath{{group}}));
	REQUIRE(root.node(group).outputCount() == 1);

	REQUIRE_FALSE(syncPathGroups(root, GraphPath{{group}})); // settles: a quiet frame reports no change
}

TEST_CASE("a document knows whether it links anything, at any depth", "[groupnav]")
{
	// What gates Reload Linked Groups: an item that is always enabled says nothing about whether it
	// has work to do. The nesting matters — a link is most often INSIDE an inline group, and a scan of
	// the root alone would report a document with one as having none.
	Graph root;
	REQUIRE_FALSE(hasLinkedGroups(root));

	const NodeId wrapper = addGroup<InlineGroupNode>(root, "wrapper");
	REQUIRE_FALSE(hasLinkedGroups(root)); // an inline group is not a link

	Graph& inner = static_cast<InlineGroupNode&>(root.node(wrapper)).inner();
	addGroup<LinkedGroupNode>(inner, "linked");
	REQUIRE(hasLinkedGroups(root));
}

// --- Layout migration (the pure half of the group gestures) ---------------------------------------

TEST_CASE("descendLayout moves the grouped nodes' positions into the group's subtree", "[groupnav]")
{
	// Group Selected's layout half. The nodes keep their ids across the move, so this is a transfer
	// between two maps under unchanged keys — and a node whose entry did NOT travel would silently
	// reappear in a default column, which reads as a bug somewhere else entirely.
	const NodeId group = NodeId::generate();
	const NodeId moved1 = NodeId::generate();
	const NodeId moved2 = NodeId::generate();
	const NodeId stays = NodeId::generate();

	serialize::EditorTree level;
	level.nodes[moved1] = pos(10.0, 20.0);
	level.nodes[moved2] = pos(30.0, 40.0);
	level.nodes[stays] = pos(50.0, 60.0);

	descendLayout(level, group, {moved1, moved2});

	REQUIRE(level.nodes.count(moved1) == 0); // gone from this level ...
	REQUIRE(level.nodes.count(moved2) == 0);
	REQUIRE(level.nodes.count(stays) == 1); // ... and an unselected neighbour is untouched

	const serialize::EditorTree& inner = level.groups.at(group);
	REQUIRE(inner.nodes.size() == 2); // ... and arrived below, verbatim
	REQUIRE(at(inner.nodes, moved1) == std::make_pair(10.0, 20.0));
	REQUIRE(at(inner.nodes, moved2) == std::make_pair(30.0, 40.0));
}

TEST_CASE("descendLayout skips a node with no recorded position", "[groupnav]")
{
	// A node added this frame has no entry yet. Inventing a placeholder would put it at the origin,
	// which is worse than letting the canvas lay it out.
	const NodeId group = NodeId::generate();
	const NodeId unplaced = NodeId::generate();
	serialize::EditorTree level;

	descendLayout(level, group, {unplaced});
	REQUIRE(level.groups.at(group).nodes.empty());
}

TEST_CASE("liftedPositions centres the group's contents on where the group sat", "[groupnav]")
{
	// Ungroup's layout half. An inner graph's grid space starts near the origin, so the inner
	// coordinates taken verbatim would fling the lifted nodes to a corner of a canvas the user is not
	// looking at. What must survive is their arrangement RELATIVE to each other.
	const NodeId a = NodeId::generate();
	const NodeId b = NodeId::generate();
	serialize::EditorData inner;
	inner[a] = pos(0.0, 0.0);
	inner[b] = pos(100.0, 0.0); // 100 apart, centred on (50, 0)

	const std::vector<lain::math::Vec2f> lifted = liftedPositions(inner, {a, b}, lain::math::Vec2f{500.0f, 300.0f});
	REQUIRE(lifted.size() == 2);
	REQUIRE(lifted[0].x == 450.0f); // centre of mass moved to the group's position ...
	REQUIRE(lifted[0].y == 300.0f);
	REQUIRE(lifted[1].x == 550.0f); // ... and they are still 100 apart
	REQUIRE(lifted[1].y == 300.0f);
}

TEST_CASE("liftedPositions falls back to the group's own position", "[groupnav]")
{
	// A node the inner layout never recorded (added inside the group and not yet saved) lands on the
	// group rather than at the origin — visible, and next to its neighbours.
	const NodeId unplaced = NodeId::generate();
	const std::vector<lain::math::Vec2f> lifted =
		liftedPositions(serialize::EditorData{}, {unplaced}, lain::math::Vec2f{40.0f, 70.0f});
	REQUIRE(lifted.size() == 1);
	REQUIRE(lifted[0].x == 40.0f);
	REQUIRE(lifted[0].y == 70.0f);

	REQUIRE(liftedPositions(serialize::EditorData{}, {}, lain::math::Vec2f{1.0f, 2.0f}).empty());
}

TEST_CASE("layout migration carries a nested group's whole subtree", "[groupnav]")
{
	// Grouping a selection that CONTAINS a group, and ungrouping again. Nesting is arbitrarily deep,
	// so moving only this level's positions would leave everything inside the grouped group in default
	// columns the next time it was opened — and nothing would say so, which is what makes it worth a
	// test rather than an eyeball.
	const NodeId outer = NodeId::generate();  // the group being created
	const NodeId nested = NodeId::generate(); // a group that is part of the selection
	const NodeId deep = NodeId::generate();	  // a node living inside THAT

	serialize::EditorTree level;
	level.nodes[nested] = pos(10.0, 10.0);
	level.groups[nested].nodes[deep] = pos(77.0, 88.0);

	descendLayout(level, outer, {nested});
	REQUIRE(level.groups.count(nested) == 0); // the subtree left this level ...
	const serialize::EditorTree& inside = level.groups.at(outer);
	REQUIRE(at(inside.nodes, nested) == std::make_pair(10.0, 10.0));
	REQUIRE(at(inside.groups.at(nested).nodes, deep) == std::make_pair(77.0, 88.0)); // ... intact

	// And back out: the subtree returns to this level, and the emptied group's own level is dropped.
	ascendLayout(level, outer, {nested});
	REQUIRE(level.groups.count(outer) == 0);
	REQUIRE(at(level.groups.at(nested).nodes, deep) == std::make_pair(77.0, 88.0));
}

TEST_CASE("a path step selects WHICH evaluation, not just which node", "[groupnav]")
{
	// Until maps existed, a group's graph walk and its evaluation walk were the same sequence of
	// node ids, so a bare NodeId said everything a path needed to. A MAP has one child Evaluation
	// per element (ADR-0014), so the step carries the element too — and the evaluation walk is the
	// only one where it matters: the graph walk still reaches ONE definition however many elements
	// are being evaluated over it.
	Graph root;
	const NodeId group = root.add<InlineGroupNode>();
	Evaluation evaluation{root};

	// Stand in for a map's N children directly. What is under test here is the WALK; sizing them
	// from a collection is the scheduler's job and is covered against the real thing in libs/flow.
	evaluation.setChildCount(group, 3);
	REQUIRE(evaluation.childCount(group) == 3);

	REQUIRE(&resolveEvaluation(evaluation, GraphPath{{group, 0}}) == &evaluation.child(group, 0));
	REQUIRE(&resolveEvaluation(evaluation, GraphPath{{group, 2}}) == &evaluation.child(group, 2));
	// Not merely equal — a different element is a different Evaluation, which is what keeps two
	// streams' values apart.
	REQUIRE(&evaluation.child(group, 0) != &evaluation.child(group, 2));

	SECTION("an element that is not there stops the walk, like any other stale step")
	{
		REQUIRE(&resolveEvaluation(evaluation, GraphPath{{group, 9}}) == &evaluation);
	}

	SECTION("the default element is 0, so every existing path still resolves")
	{
		REQUIRE(&resolveEvaluation(evaluation, GraphPath{{group}}) == &evaluation.child(group, 0));
	}
}

TEST_CASE("a map crumb reports that it is per-element, and how many", "[groupnav]")
{
	// The breadcrumb's element stepper needs two facts from two different owners: the DEFINITION
	// says a step is per-element (a map), and the EVALUATION says how many elements there turned out
	// to be. Neither can answer the other — a map's arity is not in the recipe at all — which is why
	// these are two functions rather than one.
	Graph root;
	const NodeId group = root.add<InlineGroupNode>();
	const NodeId map = root.add<MapNode>();
	Evaluation evaluation{root};
	evaluation.setChildCount(map, 4);

	const std::vector<Crumb> viaGroup = breadcrumb(root, GraphPath{{group}});
	REQUIRE(viaGroup.size() == 2);
	REQUIRE(viaGroup[1].interior == InteriorEvaluation::Once); // an ordinary group: one evaluation

	const std::vector<Crumb> viaMap = breadcrumb(root, GraphPath{{map}});
	REQUIRE(viaMap.size() == 2);
	REQUIRE(viaMap[1].interior == InteriorEvaluation::PerElement);

	const std::vector<std::size_t> counts = pathElementCounts(evaluation, GraphPath{{map, 2}});
	REQUIRE(counts.size() == 1);
	REQUIRE(counts[0] == 4); // how far the stepper may go

	SECTION("a step with nothing prepared reports zero rather than guessing")
	{
		Evaluation fresh{root};
		const std::vector<std::size_t> none = pathElementCounts(fresh, GraphPath{{map, 0}});
		REQUIRE(none.size() == 1);
		REQUIRE(none[0] == 0); // a map has no children until a run has sized it
	}
}

TEST_CASE("a map's interior is editable, a linked group's is not", "[groupnav]")
{
	// resolveEditable asks the NODE whether its interior is its own to hand out, rather than testing
	// for InlineGroupNode. When the map arrived it became a second kind that owns one, and a class
	// test would have made it silently read-only — a map you could descend into and never build.
	Graph root;
	const NodeId group = root.add<InlineGroupNode>();
	const NodeId map = root.add<MapNode>();
	const NodeId linked = root.add<LinkedGroupNode>();

	REQUIRE(resolveEditable(root, GraphPath{{group}}) != nullptr);
	REQUIRE(resolveEditable(root, GraphPath{{map}}) != nullptr);
	REQUIRE(resolveEditable(root, GraphPath{{linked}}) == nullptr);

	// And the per-frame reconciliation descends into a map too, so editing its interface from the
	// inside reaches its outer ports (M5's bug three, in its map-shaped form).
	// A map can only mirror a pin whose LIST form is registered — that is how it lifts an inner T to
	// an outer vector<T> at runtime (ADR-0014). Without it the pin is refused rather than silently
	// mirrored un-lifted, which is what this registration makes visible.
	registerPortType<std::vector<int>>("ListOfInt");

	auto& inner = static_cast<MapNode&>(root.node(map)).inner();
	inner.boundaryInputNode().addBoundary<int>("item");
	REQUIRE(syncPathGroups(root, GraphPath{{map}}));
	REQUIRE(root.node(map).inputCount() == 1);
}

TEST_CASE("every crumb's depth indexes the path it was built from", "[groupnav]")
{
	// The breadcrumb's element stepper turns a crumb's depth into a path index (`depth - 1`) to read
	// which element that map is showing. This pins down that the arithmetic is always in range —
	// a crumb never claims a depth the path does not have, and a per-element crumb is never the root.
	//
	// The OTHER half of that crash was not arithmetic: the strip read the LIVE path while drawing
	// crumbs computed from it, and a crumb click replaces the path mid-loop — so clicking the
	// document crumb emptied the path and the map crumb behind it indexed element 0 of nothing. That
	// is fixed by drawing the whole strip from one snapshot, which no unit test can express; it lives
	// in GraphPane::draw and needs the window.
	registerPortType<std::vector<int>>("ListOfInt");
	Graph root;
	const NodeId group = root.add<InlineGroupNode>();
	auto& groupInner = static_cast<InlineGroupNode&>(root.node(group)).inner();
	const NodeId map = groupInner.add<MapNode>();

	const GraphPath path{{group}, {map, 2}};
	const std::vector<Crumb> crumbs = breadcrumb(root, path);
	REQUIRE(crumbs.size() == 3); // document, group, map

	for (const Crumb& crumb : crumbs)
	{
		REQUIRE(crumb.depth <= path.size()); // never names a step the path does not have
		if (crumb.interior == InteriorEvaluation::PerElement)
			REQUIRE(crumb.depth >= 1); // the root is not an element of anything
	}

	// And the counts line up with the same indexing, so the two are read together safely.
	Evaluation evaluation{root};
	const std::vector<std::size_t> counts = pathElementCounts(evaluation, path);
	REQUIRE(counts.size() == path.size());
	REQUIRE(crumbs.back().interior == InteriorEvaluation::PerElement);
	REQUIRE(crumbs.back().depth - 1 < counts.size());
}
