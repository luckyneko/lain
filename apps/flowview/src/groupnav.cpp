#include "groupnav.h"

#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>

#include <algorithm>
#include <iterator>

namespace flowview
{
	using namespace lain;

	const flow::Graph& resolvePath(const flow::Graph& root, GraphPath& path)
	{
		const flow::Graph* current = &root;
		std::size_t resolved = 0;
		for (const flow::NodeId step : path)
		{
			if (!current->contains(step))
				break;
			const flow::Graph* inner = current->node(step).innerGraph();
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
		for (const flow::NodeId step : path)
		{
			if (!current->contains(step))
				return nullptr;
			// Only an INLINE group hands out a mutable interior — which is the whole point: a linked
			// group has no such accessor, so the walk simply cannot continue into one.
			auto* group = dynamic_cast<flow::InlineGroupNode*>(&current->node(step));
			if (group == nullptr)
				return nullptr;
			current = &group->inner();
		}
		return current;
	}

	flow::Evaluation& resolveEvaluation(flow::Evaluation& root, const GraphPath& path)
	{
		flow::Evaluation* current = &root;
		for (const flow::NodeId step : path)
		{
			if (!current->hasChild(step))
				break; // no child prepared for this group (yet) — stop where the values actually are
			current = &current->child(step);
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
			if (!current->contains(path[i]))
				break;
			const flow::Node& node = current->node(path[i]);
			const flow::Graph* inner = node.innerGraph();
			if (inner == nullptr)
				break;
			crumbs.push_back(Crumb{node.name(), i + 1});
			current = inner;
		}
		return crumbs;
	}

	const flow::LinkedGroupNode* enclosingLinkedGroup(const flow::Graph& root, const GraphPath& path)
	{
		const flow::Graph* current = &root;
		for (const flow::NodeId step : path)
		{
			if (!current->contains(step))
				break;
			const flow::Node& node = current->node(step);
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

	bool syncPathGroups(flow::Graph& root, const GraphPath& path)
	{
		bool changed = false;
		flow::Graph* parent = &root;
		for (const flow::NodeId step : path)
		{
			if (!parent->contains(step))
				break;
			// Only descend through EDITABLE levels. Syncing a group that lives inside a linked group
			// would re-derive ports in the TEMPLATE's graph — harmless while every instance held its
			// own copy, wrong the moment that graph is shared (ADR-0013). A template's internal
			// mirroring was settled when the template itself was loaded.
			auto* group = dynamic_cast<flow::InlineGroupNode*>(&parent->node(step));
			if (group == nullptr)
				break;
			// Sync the group IN its parent — only the parent can disconnect edges a dropped pin frees.
			changed |= flow::edit::syncGroupPorts(*parent, step).changed();
			parent = &group->inner();
		}
		return changed;
	}

	flow::serialize::EditorTree& layoutAt(flow::serialize::EditorTree& root, const GraphPath& path)
	{
		flow::serialize::EditorTree* current = &root;
		for (const flow::NodeId step : path)
			current = &current->groups[step]; // default-constructs the level if it is new
		return *current;
	}

	const flow::serialize::EditorTree* findLayoutAt(const flow::serialize::EditorTree& root, const GraphPath& path)
	{
		const flow::serialize::EditorTree* current = &root;
		for (const flow::NodeId step : path)
		{
			const auto it = current->groups.find(step);
			if (it == current->groups.end())
				return nullptr;
			current = &it->second;
		}
		return current;
	}
} // namespace flowview
