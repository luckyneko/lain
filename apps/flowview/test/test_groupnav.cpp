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
		GraphPath path{NodeId::generate()};
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

	// The trap, made explicit: the nested link is NOT in the root, so an id-based lookup there fails.
	// It used to fail silently and find a different node, because ids restarted per Graph; a uuid
	// makes it an honest miss (ADR-0011). Either way an id alone does not say which graph to look in,
	// which is why this returns the node itself.
	REQUIRE_FALSE(root.contains(linked));
	REQUIRE(mid.contains(linked));

	REQUIRE(enclosingLinkedGroup(root, GraphPath{outer, linked}) == &mid.node(linked));
	REQUIRE_FALSE(editableAt(root, GraphPath{outer, linked}));
	REQUIRE(editableAt(root, GraphPath{outer})); // the wrapper itself is still editable
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
	layoutAt(tree, GraphPath{group}).nodes[node] = innerBlob;

	const auto* rootLevel = findLayoutAt(tree, GraphPath{});
	const auto* innerLevel = findLayoutAt(tree, GraphPath{group});
	REQUIRE(rootLevel != nullptr);
	REQUIRE(innerLevel != nullptr);
	REQUIRE(rootLevel->nodes.at(group).find("x")->asDouble() == 10.0);
	REQUIRE(innerLevel->nodes.at(node).find("x")->asDouble() == 99.0); // not the root's 10.0

	SECTION("an unvisited level simply has no stored layout")
	{
		REQUIRE(findLayoutAt(tree, GraphPath{NodeId::generate()}) == nullptr);
	}

	SECTION("layoutAt creates a level on demand, so a first visit can record into it")
	{
		const NodeId unvisited = NodeId::generate();
		layoutAt(tree, GraphPath{unvisited}).nodes[NodeId::generate()] = rootBlob;
		REQUIRE(findLayoutAt(tree, GraphPath{unvisited}) != nullptr);
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
