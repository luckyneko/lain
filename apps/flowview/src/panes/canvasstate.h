#pragma once

#include <lain/flow/serialize/loadresult.h> // EditorData (collectLayout's return)
#include <lain/flow/types.h>				// NodeId

#include <vector>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	class CanvasIds;

	// Reading the imnodes canvas back in domain terms. The *writing* half (which int names what) is
	// CanvasIds; these are the two questions the panes ask of the canvas that is currently on screen,
	// which is why they need imnodes and CanvasIds does not.

	// The nodes currently selected on the canvas (imnodes' global selection), as ids. Read by the
	// Graph canvas (Delete) and the Inspector (its selection-driven view). An int the mapping does
	// not know is skipped — imnodes' selection pool can outlive the object it referred to.
	std::vector<lain::flow::NodeId> selectedNodes(const CanvasIds& ids);

	// The current canvas node positions as an EditorData blob (NodeId -> {x, y}), read from imnodes
	// grid space. Must run while the canvas' node ids are live (during a frame). Shared by Save (the
	// editor section of a serialized graph) and the undo snapshot.
	lain::flow::serialize::EditorData collectLayout(CanvasIds& ids, const lain::flow::Graph& graph);
} // namespace flowview
