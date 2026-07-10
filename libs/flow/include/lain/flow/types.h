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
	// by which accessor — input() / output() — it is used with). A PortIndex is only a
	// positional *cursor* for iterating a node's ports — never a durable reference: a
	// port that is added/removed/reordered keeps its PortId but not its index.
	using PortIndex = std::size_t;

	// A port's stable identity within its node — the port-level analogue of NodeId. A
	// port keeps its PortId when sibling ports are added, removed, or reordered, so an
	// edge (and a boundary handle) survives dynamic-port mutation. Scoped per node (an
	// edge pairs it with a NodeId), so a 32-bit counter is ample; 0 is the reserved "no
	// port" sentinel, real ids start at 1.
	class PortId
	{
	public:
		PortId() = default;
		explicit constexpr PortId(std::uint32_t value)
			: m_value(value)
		{
		}

		constexpr std::uint32_t value() const { return m_value; }

		friend constexpr bool operator==(PortId a, PortId b) { return a.m_value == b.m_value; }
		friend constexpr bool operator!=(PortId a, PortId b) { return a.m_value != b.m_value; }
		friend constexpr bool operator<(PortId a, PortId b) { return a.m_value < b.m_value; }

	private:
		std::uint32_t m_value = 0;
	};

	// The durable address of one port on one node: the unit an edge and a boundary handle
	// reference (an Edge is two PortAddresses, {from, to}), and the serialization primitive
	// (a connection is {from, to}). Holds no direction — an edge implies it by position, a
	// lone address derives it from Port::direction().
	struct PortAddress
	{
		NodeId node;
		PortId port;

		friend constexpr bool operator==(PortAddress a, PortAddress b) { return a.node == b.node && a.port == b.port; }
		friend constexpr bool operator!=(PortAddress a, PortAddress b) { return !(a == b); }
	};

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

template <>
struct std::hash<lain::flow::PortId>
{
	std::size_t operator()(lain::flow::PortId id) const noexcept
	{
		return std::hash<std::uint32_t>{}(id.value());
	}
};

// Hash a PortAddress so it keys selection / lookup — combine the node + port hashes.
template <>
struct std::hash<lain::flow::PortAddress>
{
	std::size_t operator()(lain::flow::PortAddress a) const noexcept
	{
		const std::size_t h = std::hash<lain::flow::NodeId>{}(a.node);
		return h ^ (std::hash<lain::flow::PortId>{}(a.port) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
	}
};
