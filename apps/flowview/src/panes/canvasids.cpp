#include "canvasids.h"

#include <lain/gui/nodes.h>

#include <cstddef>
#include <cstdint>

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
} // namespace flowview
