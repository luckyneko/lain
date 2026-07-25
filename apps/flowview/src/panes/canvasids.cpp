#include "canvasids.h"

#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/gui/nodes.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace flowview
{
	using namespace lain;

	int pinId(flow::NodeId node, bool output, flow::PortId port)
	{
		return static_cast<int>(node.value()) * 1000 + (output ? 500 : 0) + static_cast<int>(port.value());
	}

	std::vector<flow::NodeId> selectedNodes()
	{
		std::vector<flow::NodeId> out;
		const int selected = gui::nodes::NumSelectedNodes();
		if (selected <= 0)
			return out;

		std::vector<int> nodeIds(static_cast<std::size_t>(selected));
		gui::nodes::GetSelectedNodes(nodeIds.data());
		for (const int id : nodeIds)
			out.push_back(flow::NodeId{static_cast<std::uint64_t>(id)});
		return out;
	}

	flow::serialize::EditorData collectLayout(const flow::Graph& graph)
	{
		flow::serialize::EditorData layout;
		for (const flow::NodeId id : graph.nodeIds())
		{
			const ImVec2 pos = gui::nodes::GetNodeGridSpacePos(static_cast<int>(id.value()));
			data::Value blob = data::Value::object();
			blob.set("x", data::Value(static_cast<double>(pos.x)));
			blob.set("y", data::Value(static_cast<double>(pos.y)));
			layout[id] = std::move(blob);
		}
		return layout;
	}
} // namespace flowview
