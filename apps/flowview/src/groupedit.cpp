#include "groupedit.h"

#include "appcontext.h"
#include "flowviewapp.h"
#include "graphio.h"
#include "panes/canvasstate.h" // selectedNodes / collectLayout — the canvas half of a gesture

#include <lain/data/value.h>
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/serialize/serialize.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/nodes.h>
#include <lain/math/types.h>
#include <lain/string/format.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// --- reading the canvas ----------------------------------------------------------------------

	// The selection, restricted to nodes actually present in the graph on screen. Both filters earn
	// their keep: imnodes' selection pool can outlive what it named, and CanvasIds spans the whole
	// DOCUMENT, so an int can still decode to a perfectly real node at another level.
	static std::vector<flow::NodeId> selectionIn(const AppContext& ctx, const flow::Graph& graph)
	{
		std::vector<flow::NodeId> ids;
		for (const flow::NodeId id : selectedNodes(ctx.canvas))
		{
			if (graph.contains(id))
				ids.push_back(id);
		}
		return ids;
	}

	// The selected node's id when the selection is exactly one node of type T, else a null id — which
	// is both "is this gesture available?" and "what does it act on", asked once.
	template <typename T>
	static flow::NodeId soleSelectedOfType(const AppContext& ctx, const flow::Graph& graph)
	{
		const std::vector<flow::NodeId> ids = selectionIn(ctx, graph);
		if (ids.size() != 1)
			return {};
		return dynamic_cast<const T*>(&graph.node(ids.front())) != nullptr ? ids.front() : flow::NodeId{};
	}

	static math::Vec2f nodePos(AppContext& ctx, flow::NodeId id)
	{
		return gui::nodes::GetNodeGridSpacePos(ctx.canvas.node(id));
	}

	static void setNodePos(AppContext& ctx, flow::NodeId id, math::Vec2f pos)
	{
		gui::nodes::SetNodeGridSpacePos(ctx.canvas.node(id), pos);
	}

	static math::Vec2f centroid(const std::vector<math::Vec2f>& points, math::Vec2f fallback)
	{
		if (points.empty())
			return fallback;
		math::Vec2f sum{0.0f, 0.0f};
		for (const math::Vec2f& p : points)
		{
			sum.x += p.x;
			sum.y += p.y;
		}
		const float n = static_cast<float>(points.size());
		return math::Vec2f{sum.x / n, sum.y / n};
	}

	// The level a gesture acts at, or nullptr inside a linked group (where nothing may be edited).
	// Every gesture here resolves its own level, as Add ▸ Linked Group… does — the menu bar is handed
	// only the const graph on purpose.
	static flow::Graph* editableLevel(AppContext& ctx)
	{
		return resolveEditable(ctx.app->graph(), ctx.activePath);
	}

	// --- availability ----------------------------------------------------------------------------

	bool canGroupSelection(const AppContext& ctx, const flow::Graph& activeGraph)
	{
		// A boundary node in the selection would be refused by the gesture anyway; excluding it here
		// too means the menu says so before the click rather than after it.
		const std::vector<flow::NodeId> ids = selectionIn(ctx, activeGraph);
		if (ids.empty())
			return false;
		for (const flow::NodeId id : ids)
		{
			if (id == activeGraph.boundaryInputNode().id() || id == activeGraph.boundaryOutputNode().id())
				return false;
		}
		return true;
	}

	bool canUngroupSelection(const AppContext& ctx, const flow::Graph& activeGraph)
	{
		return soleSelectedOfType<flow::InlineGroupNode>(ctx, activeGraph) != flow::NodeId{};
	}

	bool canMakeLocalSelection(const AppContext& ctx, const flow::Graph& activeGraph)
	{
		return soleSelectedOfType<flow::LinkedGroupNode>(ctx, activeGraph) != flow::NodeId{};
	}

	// --- Group Selected --------------------------------------------------------------------------

	// The wording for a refusal. Kept next to the gestures rather than in flow, which reports the fact
	// and leaves the phrasing to whoever is doing the talking.
	static std::string refusalText(flow::edit::GroupRefusal refusal)
	{
		switch (refusal)
		{
			case flow::edit::GroupRefusal::EmptySelection:
				return "Select the nodes to group first";
			case flow::edit::GroupRefusal::ContainsBoundary:
				return "The graph's own Input / Output nodes cannot be grouped";
			case flow::edit::GroupRefusal::UnnamedPinType:
				return "A value crossing this selection has an unregistered port type, so the group could "
					   "not be saved";
			case flow::edit::GroupRefusal::WouldCycle:
				return "A value leaves this selection and comes back - include the nodes in between";
			case flow::edit::GroupRefusal::NotAGroup:
				return "That node contains no graph";
			case flow::edit::GroupRefusal::NotInline:
				return "This group is linked - make it local first";
			case flow::edit::GroupRefusal::None:
				break;
		}
		return {};
	}

	bool groupSelection(AppContext& ctx, const flow::Graph& activeGraph)
	{
		flow::Graph* editable = editableLevel(ctx);
		if (editable == nullptr)
		{
			ctx.noteReadOnlyEdit();
			return false;
		}
		const std::vector<flow::NodeId> selection = selectionIn(ctx, activeGraph);

		// Where the nodes are RIGHT NOW, straight from imnodes — ctx.layout is only refreshed at save
		// and snapshot time, so it would hand back positions from before the last drag.
		const flow::serialize::EditorData live = collectLayout(ctx.canvas, activeGraph);
		std::vector<math::Vec2f> positions;
		for (const flow::NodeId id : selection)
			positions.push_back(nodePos(ctx, id));
		const math::Vec2f where = centroid(positions, math::Vec2f{0.0f, 0.0f});

		const flow::edit::GroupResult result = flow::edit::groupSelected(*editable, selection);
		if (!result.ok())
		{
			ctx.noteMessage(Issue::Severity::Warning, refusalText(result.refusal));
			return false;
		}

		// Carry the layout down a level. The moved nodes kept their NodeIds (Graph::extract), so this
		// is a move between two maps under the same keys — nothing is re-keyed, and nothing is lost.
		flow::serialize::EditorTree& here = layoutAt(ctx.layout, ctx.activePath);
		here.nodes = live;
		descendLayout(here, result.group, result.moved);

		// The group lands on the selection's centre of mass, which is where the user was looking.
		setNodePos(ctx, result.group, where);
		ctx.markChanged();
		return true;
	}

	// --- Ungroup ---------------------------------------------------------------------------------

	bool ungroupSelection(AppContext& ctx, const flow::Graph& activeGraph)
	{
		flow::Graph* editable = editableLevel(ctx);
		if (editable == nullptr)
		{
			ctx.noteReadOnlyEdit();
			return false;
		}
		const flow::NodeId group = soleSelectedOfType<flow::InlineGroupNode>(ctx, activeGraph);
		if (group == flow::NodeId{})
		{
			ctx.noteMessage(Issue::Severity::Warning, "Select a single inline group to ungroup");
			return false;
		}

		// Read the inner layout and the group's own position BEFORE the group is destroyed.
		const math::Vec2f groupPos = nodePos(ctx, group);
		GraphPath innerPath = ctx.activePath;
		innerPath.push_back(group);
		flow::serialize::EditorData innerLayout;
		if (const flow::serialize::EditorTree* tree = findLayoutAt(ctx.layout, innerPath))
			innerLayout = tree->nodes;
		const flow::serialize::EditorData live = collectLayout(ctx.canvas, activeGraph);

		const flow::edit::UngroupResult result = flow::edit::ungroup(*editable, group);
		if (!result.ok())
		{
			ctx.noteMessage(Issue::Severity::Warning, refusalText(result.refusal));
			return false;
		}

		// Land the lifted nodes around where the group sat (see liftedPositions).
		const std::vector<math::Vec2f> positions = liftedPositions(innerLayout, result.moved, groupPos);

		flow::serialize::EditorTree& here = layoutAt(ctx.layout, ctx.activePath);
		here.nodes = live;
		here.nodes.erase(group);
		ascendLayout(here, group, result.moved); // a lifted node may be a group; its subtree comes too
		for (std::size_t i = 0; i < result.moved.size(); ++i)
			setNodePos(ctx, result.moved[i], positions[i]);

		ctx.markChanged();
		return true;
	}

	// --- Save as Template ------------------------------------------------------------------------

	// `path` written relative to the open document when there is one, so a project folder stays
	// portable; absolute otherwise, which is the honest choice for an untitled document with no
	// anchor to be relative to. (The same rule Add ▸ Linked Group… follows.)
	static std::string sourceRelativeTo(const std::filesystem::path& documentDir, const std::filesystem::path& path)
	{
		if (documentDir.empty())
			return path.generic_string();
		std::error_code ec;
		const std::filesystem::path relative = std::filesystem::relative(path, documentDir, ec);
		return (ec || relative.empty()) ? path.generic_string() : relative.generic_string();
	}

	bool saveAsTemplate(AppContext& ctx, const flow::Graph& activeGraph)
	{
		flow::Graph* editable = editableLevel(ctx);
		if (editable == nullptr)
		{
			ctx.noteReadOnlyEdit();
			return false;
		}
		const flow::NodeId group = soleSelectedOfType<flow::InlineGroupNode>(ctx, activeGraph);
		if (group == flow::NodeId{})
		{
			ctx.noteMessage(Issue::Severity::Warning, "Select a single inline group to save as a template");
			return false;
		}

		const auto picked = gui::saveFile("Save group as template", gui::lastDirectory(), {{"json", {"*.json"}}});
		if (!picked)
			return false;
		std::filesystem::path file = *picked;
		file.replace_extension("json"); // the data codec is keyed off the extension

		GraphPath innerPath = ctx.activePath;
		innerPath.push_back(group);
		const flow::serialize::EditorTree& innerLayout = layoutAt(ctx.layout, innerPath);

		// Write the interior out as a document in its own right — which is all a template is.
		{
			const auto& inlineGroup = static_cast<const flow::InlineGroupNode&>(editable->node(group));
			if (!saveGraph(file.string(), inlineGroup.inner(), ctx.app->nodeFactory(), innerLayout))
			{
				gui::message("Save failed", "Could not write " + file.string(), true);
				return false;
			}
		}
		// The file just changed, so any definition cached under it is now the OLD one. Same rule as
		// Save: an invalidation that is skipped simply keeps serving what it was told to drop.
		ctx.templates.invalidate(templateKey(file));

		// Re-point the group at what we just wrote. The link is resolved through the loader's own
		// routine, so this instance shares the cached definition with every other one built from that
		// file — including the ones that appear later.
		auto linked = std::make_unique<flow::LinkedGroupNode>();
		const std::filesystem::path documentDir = ctx.currentPath.parent_path();
		linked->setSource(sourceRelativeTo(documentDir, file));
		const flow::serialize::ResolveResult resolved =
			flow::serialize::resolveLinkedGroup(*linked, ctx.app->nodeFactory(), sceneCodecs(),
												templateResolver(documentDir), &ctx.templates);
		for (const flow::serialize::LoadIssue& issue : resolved.issues)
		{
			const Issue::Severity sev = issue.severity == flow::serialize::Severity::Error ? Issue::Severity::Error
																						   : Issue::Severity::Warning;
			ctx.loadIssues.push_back({sev, "template: " + issue.message, {}});
		}

		const math::Vec2f where = nodePos(ctx, group);
		const flow::edit::ReplaceResult replaced = flow::edit::replaceGroup(*editable, group, std::move(linked));
		if (!replaced.ok())
		{
			ctx.noteMessage(Issue::Severity::Warning, refusalText(replaced.refusal));
			return false;
		}
		// The id is preserved, so the group keeps its canvas int and its place in the layout tree —
		// but its INTERIOR is now the template's, layout and all.
		layoutAt(ctx.layout, innerPath) = resolved.editor;
		setNodePos(ctx, group, where);
		if (replaced.dropped > 0)
		{
			ctx.noteMessage(Issue::Severity::Warning,
							string::format("{} link(s) dropped: the template's interface differs", replaced.dropped));
		}
		ctx.markChanged();
		return true;
	}

	// --- Make Local ------------------------------------------------------------------------------

	bool makeLocal(AppContext& ctx, const flow::Graph& activeGraph)
	{
		flow::Graph* editable = editableLevel(ctx);
		if (editable == nullptr)
		{
			ctx.noteReadOnlyEdit();
			return false;
		}
		const flow::NodeId group = soleSelectedOfType<flow::LinkedGroupNode>(ctx, activeGraph);
		if (group == flow::NodeId{})
		{
			ctx.noteMessage(Issue::Severity::Warning, "Select a single linked group to make local");
			return false;
		}

		const auto& linked = static_cast<const flow::LinkedGroupNode&>(editable->node(group));
		if (linked.source().empty())
		{
			ctx.noteMessage(Issue::Severity::Warning, "This group has no template to copy");
			return false;
		}

		// Read the template FROM DISK rather than copying the shared definition in memory. That is the
		// honest meaning of "make a local copy of it" — and it is also the only way: a definition is
		// held as a `shared_ptr<const Graph>` precisely so no instance can reach in and take it.
		//
		// No cache: what is being built here is a private body, not another sharer of that file.
		const std::filesystem::path documentDir = ctx.currentPath.parent_path();
		const std::filesystem::path target = documentDir.empty() ? std::filesystem::path(linked.source())
																 : documentDir / linked.source();
		flow::serialize::LoadResult loaded = loadGraph(target.string(), ctx.app->nodeFactory(), nullptr);
		if (loaded.graph.nodeCount() == 0)
		{
			ctx.noteMessage(Issue::Severity::Error, "Could not read the template " + target.string());
			for (const flow::serialize::LoadIssue& issue : loaded.issues)
				ctx.loadIssues.push_back({Issue::Severity::Error, "template: " + issue.message, {}});
			return false;
		}

		auto body = std::make_unique<flow::InlineGroupNode>();
		body->inner() = std::move(loaded.graph);

		const math::Vec2f where = nodePos(ctx, group);
		const flow::edit::ReplaceResult replaced = flow::edit::replaceGroup(*editable, group, std::move(body));
		if (!replaced.ok())
		{
			ctx.noteMessage(Issue::Severity::Warning, refusalText(replaced.refusal));
			return false;
		}

		GraphPath innerPath = ctx.activePath;
		innerPath.push_back(group); // the id survived the swap, so the subtree is still this group's
		layoutAt(ctx.layout, innerPath) = std::move(loaded.editor);
		setNodePos(ctx, group, where);
		if (replaced.dropped > 0)
		{
			ctx.noteMessage(Issue::Severity::Warning,
							string::format("{} link(s) dropped: the template's interface differs", replaced.dropped));
		}
		ctx.markChanged();
		return true;
	}
} // namespace flowview
