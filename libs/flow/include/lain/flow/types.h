#pragma once

#include <cstddef>

namespace lain::flow
{
	// A node's identity within its Graph (its index in the node list).
	using NodeId = std::size_t;

	// A port's index within its node's input or output list (direction is implied
	// by which accessor — input() / output() — it is used with).
	using PortIndex = std::size_t;

	// Outcome of Graph::connect — Ok, or the reason the edge was rejected.
	enum class Connection
	{
		Ok,
		InvalidNode,  // from / to is not a node in this graph
		InvalidPort,  // outPort / inPort is out of range
		TypeMismatch, // the output and input declared types differ
		InputInUse,   // the input port already has a source (disconnect first)
		WouldCycle,   // the edge would introduce a cycle
	};
}
