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

	flow::Graph& resolvePath(flow::Graph& root, GraphPath& path)
	{
		flow::Graph* current = &root;
		std::size_t resolved = 0;
		for (const flow::NodeId step : path)
		{
			if (!current->contains(step))
				break;
			flow::Graph* inner = current->node(step).innerGraph();
			if (inner == nullptr)
				break; // the node is still there but no longer contains a graph
			current = inner;
			++resolved;
		}
		path.resize(resolved); // drop whatever didn't resolve, so the path and the graph agree
		return *current;
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

	std::vector<Crumb> breadcrumb(flow::Graph& root, const GraphPath& path)
	{
		std::vector<Crumb> crumbs;
		crumbs.push_back(Crumb{"root", 0});

		flow::Graph* current = &root;
		for (std::size_t i = 0; i < path.size(); ++i)
		{
			if (!current->contains(path[i]))
				break;
			flow::Node& node = current->node(path[i]);
			flow::Graph* inner = node.innerGraph();
			if (inner == nullptr)
				break;
			crumbs.push_back(Crumb{node.name(), i + 1});
			current = inner;
		}
		return crumbs;
	}

	flow::LinkedGroupNode* enclosingLinkedGroup(flow::Graph& root, const GraphPath& path)
	{
		flow::Graph* current = &root;
		for (const flow::NodeId step : path)
		{
			if (!current->contains(step))
				break;
			flow::Node& node = current->node(step);
			flow::Graph* inner = node.innerGraph();
			if (inner == nullptr)
				break;
			// The OUTERMOST link wins: everything below it belongs to that template.
			if (auto* linked = dynamic_cast<flow::LinkedGroupNode*>(&node))
				return linked;
			current = inner;
		}
		return nullptr;
	}

	bool editableAt(flow::Graph& root, const GraphPath& path)
	{
		return enclosingLinkedGroup(root, path) == nullptr;
	}

	int countLinkedInstances(flow::Graph& root, const std::string& source)
	{
		int count = 0;
		for (const flow::NodeId id : root.nodeIds())
		{
			flow::Node& node = root.node(id);
			if (const auto* linked = dynamic_cast<const flow::LinkedGroupNode*>(&node); linked && linked->source() == source)
				++count;
			if (flow::Graph* inner = node.innerGraph())
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
			flow::Graph* inner = parent->node(step).innerGraph();
			if (inner == nullptr)
				break;
			// Sync the group IN its parent — only the parent can disconnect edges a dropped pin frees.
			changed |= flow::edit::syncGroupPorts(*parent, step).changed();
			parent = inner;
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
