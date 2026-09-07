#include "groupnav.h"

#include <lain/data/value.h> // the opaque per-node editor blob (x / y)
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace flowview
{
	using namespace lain;

	const flow::Graph& resolvePath(const flow::Graph& root, GraphPath& path)
	{
		const flow::Graph* current = &root;
		std::size_t resolved = 0;
		for (const PathStep& step : path)
		{
			if (!current->contains(step.node))
				break;
			const flow::Graph* inner = current->node(step.node).innerGraph();
			if (inner == nullptr)
				break; // the node is still there but no longer contains a graph
			current = inner;
			++resolved;
		}
		path.resize(resolved); // drop whatever didn't resolve, so the path and the graph agree
		return *current;
	}

	flow::Graph* resolveEditable(flow::Graph& root, const GraphPath& path)
	{
		flow::Graph* current = &root;
		for (const PathStep& step : path)
		{
			if (!current->contains(step.node))
				return nullptr;
			// Ask the node whether its interior is its own to hand out, rather than testing for one
			// concrete class: a linked group's is its template's and it returns nullptr, while an
			// inline group AND a map both own theirs. Casting for the class here is what would make a
			// map silently read-only.
			auto* group = dynamic_cast<flow::GroupNode*>(&current->node(step.node));
			flow::Graph* editable = group != nullptr ? group->editableInner() : nullptr;
			if (editable == nullptr)
				return nullptr;
			current = editable;
		}
		return current;
	}

	const flow::LoopNode* loopAt(const flow::Graph& root, const GraphPath& path)
	{
		if (path.empty())
			return nullptr; // the root graph is nobody's interior
		GraphPath parent(path.begin(), path.end() - 1);
		const flow::Graph& level = resolvePath(root, parent);
		// resolvePath TRUNCATES what did not resolve, so a short answer means `level` is some
		// ancestor rather than the graph holding this step — and looking the node up there would
		// find a different node, which is exactly the mistake M5's bug six made.
		if (parent.size() + 1 != path.size() || !level.contains(path.back().node))
			return nullptr;
		return dynamic_cast<const flow::LoopNode*>(&level.node(path.back().node));
	}

	flow::LoopNode* editableLoopAt(flow::Graph& root, const GraphPath& path)
	{
		if (path.empty())
			return nullptr;
		// Through resolveEditable, so a loop inside a LINKED group answers nullptr: its carries
		// belong to the template, and pairing one here would be an edit the parent document cannot
		// store. Two functions rather than one plus an "am I allowed?" check, for the reason
		// resolvePath and resolveEditable are two — a caller that may not edit is handed nothing to
		// edit through, so the check cannot be forgotten.
		const GraphPath parent(path.begin(), path.end() - 1);
		flow::Graph* level = resolveEditable(root, parent);
		if (level == nullptr || !level->contains(path.back().node))
			return nullptr;
		return dynamic_cast<flow::LoopNode*>(&level->node(path.back().node));
	}

	flow::Evaluation& resolveEvaluation(flow::Evaluation& root, const GraphPath& path)
	{
		flow::Evaluation* current = &root;
		for (const PathStep& step : path)
		{
			// The element is what makes this walk differ from the graph walk: a group has exactly
			// one child, a MAP one per element, and this is where the breadcrumb's choice lands.
			if (!current->hasChild(step.node, step.element))
				break; // no child prepared there (yet) — stop where the values actually are
			current = &current->child(step.node, step.element);
		}
		return *current;
	}

	std::vector<Crumb> breadcrumb(const flow::Graph& root, const GraphPath& path)
	{
		std::vector<Crumb> crumbs;
		crumbs.push_back(Crumb{"root", 0});

		const flow::Graph* current = &root;
		for (std::size_t i = 0; i < path.size(); ++i)
		{
			if (!current->contains(path[i].node))
				break;
			const flow::Node& node = current->node(path[i].node);
			const flow::Graph* inner = node.innerGraph();
			if (inner == nullptr)
				break;
			crumbs.push_back(Crumb{node.name(), i + 1, node.interiorEvaluation()});
			current = inner;
		}
		return crumbs;
	}

	std::vector<std::size_t> pathElementCounts(const flow::Evaluation& root, const GraphPath& path)
	{
		std::vector<std::size_t> counts;
		counts.reserve(path.size());
		const flow::Evaluation* current = &root;
		for (const PathStep& step : path)
		{
			counts.push_back(current->childCount(step.node));
			if (!current->hasChild(step.node, step.element))
				break; // nothing prepared there yet; the remaining steps have no count to report
			current = &current->child(step.node, step.element);
		}
		counts.resize(path.size(), 0); // pad, so callers can index by step without checking
		return counts;
	}

	const flow::LinkedGroupNode* enclosingLinkedGroup(const flow::Graph& root, const GraphPath& path)
	{
		const flow::Graph* current = &root;
		for (const PathStep& step : path)
		{
			if (!current->contains(step.node))
				break;
			const flow::Node& node = current->node(step.node);
			const flow::Graph* inner = node.innerGraph();
			if (inner == nullptr)
				break;
			// The OUTERMOST link wins: everything below it belongs to that template.
			if (const auto* linked = dynamic_cast<const flow::LinkedGroupNode*>(&node))
				return linked;
			current = inner;
		}
		return nullptr;
	}

	int countLinkedInstances(const flow::Graph& root, const std::string& source)
	{
		int count = 0;
		for (const flow::NodeId id : root.nodeIds())
		{
			const flow::Node& node = root.node(id);
			if (const auto* linked = dynamic_cast<const flow::LinkedGroupNode*>(&node); linked && linked->source() == source)
				++count;
			if (const flow::Graph* inner = node.innerGraph())
				count += countLinkedInstances(*inner, source); // a template may be used inside a group too
		}
		return count;
	}

	bool hasLinkedGroups(const flow::Graph& root)
	{
		for (const flow::NodeId id : root.nodeIds())
		{
			const flow::Node& node = root.node(id);
			if (dynamic_cast<const flow::LinkedGroupNode*>(&node) != nullptr)
				return true;
			if (const flow::Graph* inner = node.innerGraph(); inner && hasLinkedGroups(*inner))
				return true; // a link can sit inside an inline group, or inside another template
		}
		return false;
	}

	bool syncPathGroups(flow::Graph& root, const GraphPath& path)
	{
		bool changed = false;
		flow::Graph* parent = &root;
		for (const PathStep& step : path)
		{
			if (!parent->contains(step.node))
				break;
			// Only descend through EDITABLE levels. Syncing a group that lives inside a linked group
			// would re-derive ports in the TEMPLATE's graph — harmless while every instance held its
			// own copy, wrong the moment that graph is shared (ADR-0013). A template's internal
			// mirroring was settled when the template itself was loaded.
			auto* group = dynamic_cast<flow::GroupNode*>(&parent->node(step.node));
			flow::Graph* editable = group != nullptr ? group->editableInner() : nullptr;
			if (editable == nullptr)
				break;
			// Sync the group IN its parent — only the parent can disconnect edges a dropped pin frees.
			changed |= flow::edit::syncGroupPorts(*parent, step.node).changed();
			parent = editable;
		}
		return changed;
	}

	flow::serialize::EditorTree& layoutAt(flow::serialize::EditorTree& root, const GraphPath& path)
	{
		flow::serialize::EditorTree* current = &root;
		// Keyed by NODE alone, deliberately: a layout describes the DEFINITION, and every element of
		// a map shares one interior and so one arrangement. Only values differ per element.
		for (const PathStep& step : path)
			current = &current->groups[step.node]; // default-constructs the level if it is new
		return *current;
	}

	const flow::serialize::EditorTree* findLayoutAt(const flow::serialize::EditorTree& root, const GraphPath& path)
	{
		const flow::serialize::EditorTree* current = &root;
		for (const PathStep& step : path)
		{
			const auto it = current->groups.find(step.node);
			if (it == current->groups.end())
				return nullptr;
			current = &it->second;
		}
		return current;
	}

	void descendLayout(flow::serialize::EditorTree& parent, flow::NodeId group,
					   const std::vector<flow::NodeId>& moved)
	{
		flow::serialize::EditorTree& inner = parent.groups[group];
		for (const flow::NodeId id : moved)
		{
			if (const auto it = parent.nodes.find(id); it != parent.nodes.end())
			{
				inner.nodes[id] = it->second;
				parent.nodes.erase(it);
			}
			// A moved node may ITSELF be a group, and its own subtree has to travel with it — a graph
			// is nested arbitrarily deep, so moving only this level's positions would leave everything
			// inside a grouped group in default columns the next time it is opened.
			if (const auto it = parent.groups.find(id); it != parent.groups.end())
			{
				inner.groups[id] = std::move(it->second);
				parent.groups.erase(it);
			}
		}
	}

	void ascendLayout(flow::serialize::EditorTree& parent, flow::NodeId group,
					  const std::vector<flow::NodeId>& moved)
	{
		const auto groupIt = parent.groups.find(group);
		if (groupIt == parent.groups.end())
			return;
		// The mirror of descendLayout's second half. Their POSITIONS are not moved here — those have to
		// be translated onto the group's own spot (liftedPositions), which their nested contents do not.
		for (const flow::NodeId id : moved)
		{
			const auto it = groupIt->second.groups.find(id);
			if (it != groupIt->second.groups.end())
				parent.groups[id] = std::move(it->second); // std::map: inserting keeps groupIt valid
		}
		parent.groups.erase(group); // the group is gone, and with it the level it described
	}

	// A layout blob's position, or `fallback` when it has none — the record shape ("x" / "y" in an
	// opaque data::Value) and the missing case, in one place.
	static math::Vec2f layoutPos(const flow::serialize::EditorData& layout, flow::NodeId id, math::Vec2f fallback)
	{
		const auto it = layout.find(id);
		if (it == layout.end())
			return fallback;
		const data::Value* x = it->second.find("x");
		const data::Value* y = it->second.find("y");
		if (x == nullptr || y == nullptr)
			return fallback;
		return math::Vec2f{static_cast<float>(x->asDouble().value_or(fallback.x)),
						   static_cast<float>(y->asDouble().value_or(fallback.y))};
	}

	std::vector<math::Vec2f> liftedPositions(const flow::serialize::EditorData& innerLayout,
											 const std::vector<flow::NodeId>& moved, math::Vec2f groupPos)
	{
		std::vector<math::Vec2f> positions;
		positions.reserve(moved.size());
		for (const flow::NodeId id : moved)
			positions.push_back(layoutPos(innerLayout, id, groupPos));
		if (positions.empty())
			return positions;

		// Translate by the difference of the two centres of mass, which moves the whole arrangement
		// onto the group without disturbing how the nodes sit relative to each other.
		math::Vec2f sum{0.0f, 0.0f};
		for (const math::Vec2f& p : positions)
		{
			sum.x += p.x;
			sum.y += p.y;
		}
		const float n = static_cast<float>(positions.size());
		const math::Vec2f delta{groupPos.x - sum.x / n, groupPos.y - sum.y / n};
		for (math::Vec2f& p : positions)
		{
			p.x += delta.x;
			p.y += delta.y;
		}
		return positions;
	}
} // namespace flowview
