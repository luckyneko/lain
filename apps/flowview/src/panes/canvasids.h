#pragma once

#include <lain/flow/types.h> // NodeId, PortId

#include <vector>

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
} // namespace flowview
