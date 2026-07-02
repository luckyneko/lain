#pragma once

#include <cstddef>
#include <cstdint>
#include <functional> // std::hash specialisation below

namespace lain::flow
{
	// A node's identity within its Graph. An opaque handle over a monotonic 64-bit
	// counter (fixed width so it serialises stably across platforms) — deliberately
	// *not* a position in the node list, so a node keeps its id when others are
	// removed. A default-constructed NodeId (value 0) is the reserved "no node"
	// sentinel; a Graph allocates real ids starting from 1.
	class NodeId
	{
	public:
		NodeId() = default;
		explicit constexpr NodeId(std::uint64_t value)
			: m_value(value)
		{
		}

		// The raw counter value — for display/serialisation and to key the compact
		// int handles the imnodes canvas needs (a NodeId is never one directly).
		constexpr std::uint64_t value() const { return m_value; }

		friend constexpr bool operator==(NodeId a, NodeId b) { return a.m_value == b.m_value; }
		friend constexpr bool operator!=(NodeId a, NodeId b) { return a.m_value != b.m_value; }
		friend constexpr bool operator<(NodeId a, NodeId b) { return a.m_value < b.m_value; }

	private:
		std::uint64_t m_value = 0;
	};

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
		InputInUse,	  // the input port already has a source (disconnect first)
		WouldCycle,	  // the edge would introduce a cycle
	};
} // namespace lain::flow

// Hash so NodeId keys an unordered_map/set. The Graph itself stores nodes in an
// ordered map (needs only operator<), but consumers — e.g. the viewer's canvas —
// may want the unordered form.
template <>
struct std::hash<lain::flow::NodeId>
{
	std::size_t operator()(lain::flow::NodeId id) const noexcept
	{
		return std::hash<std::uint64_t>{}(id.value());
	}
};
