#pragma once

#include <lain/flow/types.h> // NodeId, PortId, PortAddress

#include <optional>
#include <unordered_map>

namespace flowview
{
	// The imnodes boundary: imnodes identifies every node, pin and link by a plain `int`, and a
	// NodeId is a uuid. This owns the translation, in both directions, for the life of one document.
	//
	// Ids are allocated MONOTONICALLY and never recycled, so one int names one object for as long as
	// the document is open. That matters more than compactness: imnodes keeps per-object state
	// (position, selection, drag) keyed by these ints, so an int that later names a different object
	// hands the newcomer the old one's state. The naive alternative — a dense table rebuilt each
	// frame — is worse than the cast it replaces, because deleting a node shifts every later index
	// and, within that same frame, an int names something other than what imnodes has state for.
	//
	// An EDGE needs no identity of its own: Graph::connect is single-source, so an edge IS its
	// destination PortAddress. A PIN needs its direction alongside the address, because a PortId is
	// minted per NODE — a node's first input and first output are both PortId{1}.
	//
	// None of these integers is ever serialized: the document is keyed by uuid throughout.
	//
	// WHAT THIS DOES NOT FIX: descending into a group still has to re-seed node positions and clear
	// the canvas selection. imnodes destroys a node's data the first frame it is not submitted, and
	// frees selection-pool indices without pruning them — hazards that sit below the id layer, so
	// unique ids do not address them. (ADR-0011.)
	class CanvasIds
	{
	public:
		// A pin: which port, on which side of its node.
		struct Pin
		{
			lain::flow::PortAddress address;
			bool output = false;
		};

		// The canvas int for an object, allocating one on first sight. Stable for the document's life.
		int node(lain::flow::NodeId id);
		int pin(lain::flow::PortAddress address, bool output);
		int link(lain::flow::PortAddress destination); // an edge is addressed by the input it feeds

		// What a canvas int stands for, or nullopt if it was never allocated (an id from a previous
		// document, or one imnodes invented). Callers must tolerate the nullopt: imnodes reports
		// hovered/dragged/destroyed objects after the fact, by which point the graph may have moved on.
		std::optional<lain::flow::NodeId> toNode(int canvasId) const;
		std::optional<Pin> toPin(int canvasId) const;
		std::optional<lain::flow::PortAddress> toLink(int canvasId) const;

		// Forget everything. Called when DOCUMENT identity changes (New / Open / a template swap) —
		// not for an edit, a navigation, or an undo/redo restore, all of which preserve node ids and
		// so must preserve their canvas ints, or imnodes' per-object state would scatter.
		void reset();

	private:
		// One counter across all three kinds, so an int is unambiguous even though imnodes pools
		// nodes, pins and links separately — which makes a stray id a lookup miss rather than a
		// silent hit on the wrong kind of object. Starts at 1: imnodes treats 0 as a real id, but
		// leaving it unused keeps a default-constructed int from naming anything.
		int m_next = 1;

		std::unordered_map<lain::flow::NodeId, int> m_nodes;
		std::unordered_map<int, lain::flow::NodeId> m_nodesByCanvasId;
		// Pins are keyed by address and direction together; the map holds one entry per direction.
		std::unordered_map<lain::flow::PortAddress, int> m_inputPins;
		std::unordered_map<lain::flow::PortAddress, int> m_outputPins;
		std::unordered_map<int, Pin> m_pinsByCanvasId;
		std::unordered_map<lain::flow::PortAddress, int> m_links;
		std::unordered_map<int, lain::flow::PortAddress> m_linksByCanvasId;
	};
} // namespace flowview
