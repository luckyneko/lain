#pragma once

#include <lain/flow/serialize/loadresult.h> // EditorData (collectLayout's return)
#include <lain/flow/types.h>				   // NodeId, PortId

#include <vector>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// imnodes attribute id for a pin: node * 1000 + (output ? 500 : 0) + PortId. Shared by the Graph
	// canvas (draw / hit-test) and the Inspector (a PushID scope). Encodes the port's stable PortId
	// (not its index), so a pin's id survives sibling pins being added/removed. Assumes fewer than 500
	// ports per direction and node ids well under ~2M — fine for prototyping.
	int pinId(lain::flow::NodeId node, bool output, lain::flow::PortId port);

	// The nodes currently selected on the canvas (imnodes' global selection), as ids. Read by the
	// Graph canvas (Delete) and the Inspector (its selection-driven view). imnodes node ids are the
	// int cast of NodeId.
	std::vector<lain::flow::NodeId> selectedNodes();

	// The current canvas node positions as an EditorData blob (NodeId -> {x, y}), read from imnodes
	// grid space. Must run while the canvas' node ids are live (during a frame). Shared by Save (the
	// editor section of a serialized graph) and the undo snapshot.
	lain::flow::serialize::EditorData collectLayout(const lain::flow::Graph& graph);
} // namespace flowview
