// groupnav — flowview's model of "which graph am I looking at". Driver-free: it operates on a plain
// Graph and an EditorTree, so the navigation logic is tested without a window. The *frame ordering*
// around it (a navigation requested during a draw must be consumed before the next one resolves the
// path) lives in MainWindow and still needs a live driver; what is pinned here is everything else.

#include "groupnav.h"

#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>

#include <catch2/catch_test_macros.hpp>

using namespace lain::flow;
using flowview::breadcrumb;
using flowview::Crumb;
using flowview::editableAt;
using flowview::enclosingLinkedGroup;
using flowview::findLayoutAt;
using flowview::GraphPath;
using flowview::layoutAt;
using flowview::pathFromOrdinals;
using flowview::pathOrdinals;
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
	const NodeId outer = addGroup<GroupNode>(root, "outer");
	Graph& mid = static_cast<GroupNode&>(root.node(outer)).inner();
	const NodeId inner = addGroup<GroupNode>(mid, "inner");
	Graph& deep = static_cast<GroupNode&>(mid.node(inner)).inner();

	GraphPath path{outer, inner};
	REQUIRE(&resolvePath(root, path) == &deep);
	REQUIRE(path.size() == 2); // fully resolved, so nothing was truncated
}

TEST_CASE("a stale path degrades to the deepest level that still resolves", "[groupnav]")
{
	// The host holds a path across edits, so a group deleted from under it must not dangle.
	Graph root;
	const NodeId outer = addGroup<GroupNode>(root, "outer");
	Graph& mid = static_cast<GroupNode&>(root.node(outer)).inner();
	const NodeId inner = addGroup<GroupNode>(mid, "inner");

	SECTION("a removed step truncates the path there")
	{
		REQUIRE(mid.removeNode(inner));
		GraphPath path{outer, inner};
		REQUIRE(&resolvePath(root, path) == &mid); // stopped at the parent
		REQUIRE(path.size() == 1);
	}

	SECTION("a step that is not a group truncates too")
	{
		GraphPath path{outer, root.boundaryInputNode().id()}; // a real node, but it contains no graph
		REQUIRE(&resolvePath(root, path) == &static_cast<GroupNode&>(root.node(outer)).inner());
		REQUIRE(path.size() == 1);
	}

	SECTION("an id from another graph truncates immediately")
	{
		GraphPath path{NodeId{9999}};
		REQUIRE(&resolvePath(root, path) == &root);
		REQUIRE(path.empty());
	}
}

TEST_CASE("the breadcrumb names each level and where clicking it lands", "[groupnav]")
{
	Graph root;
	const NodeId outer = addGroup<GroupNode>(root, "denoise");
	Graph& mid = static_cast<GroupNode&>(root.node(outer)).inner();
	const NodeId inner = addGroup<GroupNode>(mid, "sharpen");

	const std::vector<Crumb> crumbs = breadcrumb(root, GraphPath{outer, inner});
	REQUIRE(crumbs.size() == 3);
	REQUIRE(crumbs[0].label == "root");
	REQUIRE(crumbs[0].depth == 0); // clicking it returns to the root
	REQUIRE(crumbs[1].label == "denoise");
	REQUIRE(crumbs[1].depth == 1);
	REQUIRE(crumbs[2].label == "sharpen");
	REQUIRE(crumbs[2].depth == 2);
}

TEST_CASE("editing is off inside a linked group, and stays off below it", "[groupnav]")
{
	Graph root;
	const NodeId inlineGroup = addGroup<GroupNode>(root, "inline");
	const NodeId linked = addGroup<LinkedGroupNode>(root, "linked");
	Graph& linkedInner = static_cast<LinkedGroupNode&>(root.node(linked)).inner();
	const NodeId nestedInLink = addGroup<GroupNode>(linkedInner, "nested");

	REQUIRE(editableAt(root, GraphPath{}));			   // the root document
	REQUIRE(editableAt(root, GraphPath{inlineGroup})); // an inline group is editable in place

	REQUIRE_FALSE(editableAt(root, GraphPath{linked}));
	REQUIRE_FALSE(editableAt(root, GraphPath{linked, nestedInLink})); // and everything under it

	// "Edit Template..." acts on the OUTERMOST link — that is the template owning everything below,
	// and the only one whose stored `source` is relative to the open document.
	REQUIRE(enclosingLinkedGroup(root, GraphPath{linked, nestedInLink}) == &root.node(linked));
	REQUIRE(enclosingLinkedGroup(root, GraphPath{inlineGroup}) == nullptr);
}

TEST_CASE("a linked group nested inside an inline group is still found", "[groupnav]")
{
	// Why enclosingLinkedGroup returns the NODE: the link can sit at any depth, so an id alone would
	// send the caller looking for it in the root graph, where it does not live. That is exactly what
	// made "Edit Template..." a no-op for a nested link.
	Graph root;
	const NodeId outer = addGroup<GroupNode>(root, "wrapper"); // an ordinary inline group...
	Graph& mid = static_cast<GroupNode&>(root.node(outer)).inner();
	const NodeId linked = addGroup<LinkedGroupNode>(mid, "linked"); // ...holding the link

	// The trap, made explicit: root.contains(linked) is TRUE. Ids restart per Graph, so the nested
	// link's id collides with the wrapper's at the root — an id-based lookup there does not come up
	// empty, it silently finds a DIFFERENT node. Returning the node itself is what avoids that.
	REQUIRE(root.contains(linked));
	REQUIRE(&root.node(linked) != &mid.node(linked));

	REQUIRE(enclosingLinkedGroup(root, GraphPath{outer, linked}) == &mid.node(linked));
	REQUIRE_FALSE(editableAt(root, GraphPath{outer, linked}));
	REQUIRE(editableAt(root, GraphPath{outer})); // the wrapper itself is still editable
}

TEST_CASE("layout is stored and found per level", "[groupnav]")
{
	// Why this matters: imnodes only knows the level on screen, and node ids REPEAT across levels, so
	// each level's positions must be kept apart — otherwise a node inherits the position of the
	// same-numbered node at another level.
	lain::flow::serialize::EditorTree tree;
	const NodeId group{3};
	const NodeId node{3}; // deliberately the SAME id, one level down

	lain::data::Value rootBlob = lain::data::Value::object();
	rootBlob.set("x", lain::data::Value(10.0));
	lain::data::Value innerBlob = lain::data::Value::object();
	innerBlob.set("x", lain::data::Value(99.0));

	layoutAt(tree, GraphPath{}).nodes[group] = rootBlob;
	layoutAt(tree, GraphPath{group}).nodes[node] = innerBlob;

	const auto* rootLevel = findLayoutAt(tree, GraphPath{});
	const auto* innerLevel = findLayoutAt(tree, GraphPath{group});
	REQUIRE(rootLevel != nullptr);
	REQUIRE(innerLevel != nullptr);
	REQUIRE(rootLevel->nodes.at(group).find("x")->asDouble() == 10.0);
	REQUIRE(innerLevel->nodes.at(node).find("x")->asDouble() == 99.0); // not the root's 10.0

	SECTION("an unvisited level simply has no stored layout")
	{
		REQUIRE(findLayoutAt(tree, GraphPath{NodeId{7}}) == nullptr);
	}

	SECTION("layoutAt creates a level on demand, so a first visit can record into it")
	{
		layoutAt(tree, GraphPath{NodeId{7}}).nodes[NodeId{1}] = rootBlob;
		REQUIRE(findLayoutAt(tree, GraphPath{NodeId{7}}) != nullptr);
	}
}

TEST_CASE("syncing the active path carries an inner interface out to the group's face", "[groupnav]")
{
	// The host's per-frame reconciliation: pins added to an inner boundary (from the Interface panel,
	// while descended) have to reach the group's own ports, and nothing else does that.
	Graph root;
	const NodeId group = addGroup<GroupNode>(root, "group");
	Graph& inner = static_cast<GroupNode&>(root.node(group)).inner();

	inner.boundaryInputNode().addBoundary<int>("in");
	REQUIRE(syncPathGroups(root, GraphPath{group}));
	REQUIRE(root.node(group).inputCount() == 1);

	// The second pin lands on the OTHER side, whose inner PortId collides with the first's.
	inner.boundaryOutputNode().addBoundary<int>("out");
	REQUIRE(syncPathGroups(root, GraphPath{group}));
	REQUIRE(root.node(group).outputCount() == 1);

	REQUIRE_FALSE(syncPathGroups(root, GraphPath{group})); // settles: a quiet frame reports no change
}

TEST_CASE("a path survives the id remap a document restore performs", "[groupnav]")
{
	// The undo/redo case. A restore rebuilds the graph with FRESH NodeIds, so the active path cannot
	// be carried as ids — it goes as ordinals. To prove that actually works, the two graphs below are
	// structurally identical but have DIFFERENT ids for the same logical group: `before` churns a node
	// first, so its id counter has moved on. If ordinals were secretly agreeing with ids, this fails.
	Graph before;
	const NodeId churn = before.add<GroupNode>(); // added...
	REQUIRE(before.removeNode(churn));			  // ...and dropped, so the next id is higher
	const NodeId groupBefore = addGroup<GroupNode>(before, "denoise");

	Graph after;
	const NodeId groupAfter = addGroup<GroupNode>(after, "denoise");

	REQUIRE(groupBefore != groupAfter); // genuinely different ids for the same logical node

	const std::vector<std::size_t> ordinals = pathOrdinals(before, GraphPath{groupBefore});
	REQUIRE(ordinals.size() == 1);

	const GraphPath restored = pathFromOrdinals(after, ordinals);
	REQUIRE(restored.size() == 1);
	REQUIRE(restored[0] == groupAfter); // landed on the same logical group, not the same number

	SECTION("nested paths convert too")
	{
		Graph& innerBefore = static_cast<GroupNode&>(before.node(groupBefore)).inner();
		const NodeId nestedBefore = addGroup<GroupNode>(innerBefore, "sharpen");
		Graph& innerAfter = static_cast<GroupNode&>(after.node(groupAfter)).inner();
		const NodeId nestedAfter = addGroup<GroupNode>(innerAfter, "sharpen");

		const GraphPath deep = pathFromOrdinals(after, pathOrdinals(before, GraphPath{groupBefore, nestedBefore}));
		REQUIRE(deep.size() == 2);
		REQUIRE(deep[0] == groupAfter);
		REQUIRE(deep[1] == nestedAfter);
	}

	SECTION("a group the restored document no longer has stops the walk at its parent")
	{
		// Undoing the very edit that CREATED the group: the restored document has no group at that
		// ordinal, so the user lands on the level above rather than on a dangling path.
		Graph without; // just the boundary pair — no group at all
		REQUIRE(pathFromOrdinals(without, ordinals).empty());
	}

	SECTION("an ordinal naming a non-group stops the walk too")
	{
		Graph plain;
		plain.add<GroupNode>(); // ordinal 2 IS a group here...
		const std::vector<std::size_t> deepOrdinals{2, 0};
		const GraphPath path = pathFromOrdinals(plain, deepOrdinals);
		REQUIRE(path.size() == 1); // ...but ordinal 0 inside it is the boundary node, which contains nothing
	}
}
