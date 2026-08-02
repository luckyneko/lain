#include "canvasstate.h"

#include "../canvasids.h"

#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/gui/nodes.h>

#include <cstddef>
#include <utility>

namespace flowview
{
	using namespace lain;

	std::vector<flow::NodeId> selectedNodes(const CanvasIds& ids)
	{
		std::vector<flow::NodeId> out;
		const int selected = gui::nodes::NumSelectedNodes();
		if (selected <= 0)
			return out;

		std::vector<int> canvasIds(static_cast<std::size_t>(selected));
		gui::nodes::GetSelectedNodes(canvasIds.data());
		for (const int canvasId : canvasIds)
		{
			// A miss is expected, not a bug: imnodes frees a selection-pool index without pruning the
			// selection, so an id here can outlive what it named.
			if (const auto id = ids.toNode(canvasId))
				out.push_back(*id);
		}
		return out;
	}

	flow::serialize::EditorData collectLayout(CanvasIds& ids, const flow::Graph& graph)
	{
		flow::serialize::EditorData layout;
		for (const flow::NodeId id : graph.nodeIds())
		{
			const ImVec2 pos = gui::nodes::GetNodeGridSpacePos(ids.node(id));
			data::Value blob = data::Value::object();
			blob.set("x", data::Value(static_cast<double>(pos.x)));
			blob.set("y", data::Value(static_cast<double>(pos.y)));
			layout[id] = std::move(blob);
		}
		return layout;
	}
} // namespace flowview
