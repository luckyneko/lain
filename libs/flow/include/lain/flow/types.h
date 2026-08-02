#pragma once

#include <lain/core/uuid.h>

#include <cstddef>
#include <cstdint>
#include <functional> // std::hash specialisation below
#include <string>

namespace lain::flow
{
	// A node's identity — a core::Uuid (RFC 9562 v7), minted when the node is added to a Graph and
	// immutable thereafter. Deliberately *not* a position in the node list, and no longer a
	// per-graph counter either: group nodes made every group own a Graph, so a counter made
	// {node, port} name a pin in *every* graph at once (ADR-0011). A uuid is unique without a
	// coordinator, which is what two people editing documents on two laptops need.
	//
	// A default-constructed NodeId (the nil uuid) is the reserved "no node" sentinel.
	//
	// Identity order is NOT node order: comparison exists so a NodeId can key an ordered map, while
	// Graph tracks insertion order in its own vector (see Graph::nodeIds).
	class NodeId
	{
	public:
		NodeId() = default;
		explicit NodeId(core::Uuid uuid)
			: m_uuid(uuid)
		{
		}

		// Mint a fresh identity. Graph does this for every node it admits; a caller restoring a
		// saved node passes the id it read instead (Graph::add's requestedId overload).
		static NodeId generate() { return NodeId{core::Uuid::generate()}; }

		const core::Uuid& uuid() const { return m_uuid; }

		// The canonical lowercase uuid — what serialization writes. shortString() is the truncated
		// DISPLAY form (a node header, a log line, the cli dump), never a key.
		std::string toString() const { return m_uuid.toString(); }
		std::string shortString() const { return m_uuid.shortString(); }

		friend bool operator==(const NodeId& a, const NodeId& b) { return a.m_uuid == b.m_uuid; }
		friend bool operator!=(const NodeId& a, const NodeId& b) { return !(a == b); }
		friend bool operator<(const NodeId& a, const NodeId& b) { return a.m_uuid < b.m_uuid; }

	private:
		core::Uuid m_uuid; // nil until minted or restored
	};

	// A port's index within its node's input or output list (direction is implied
	// by which accessor — input() / output() — it is used with). A PortIndex is only a
	// positional *cursor* for iterating a node's ports — never a durable reference: a
	// port that is added/removed/reordered keeps its PortId but not its index.
	using PortIndex = std::size_t;

	// A port's stable identity within its node — the port-level analogue of NodeId. A
	// port keeps its PortId when sibling ports are added, removed, or reordered, so an
	// edge (and a boundary handle) survives dynamic-port mutation. Scoped per node — {NodeId,
	// PortId} is globally unique the moment the NodeId is, which is why only NodeId needed to
	// become a uuid — so a 32-bit counter is ample; 0 is the reserved "no port" sentinel, real ids
	// start at 1.
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

		friend bool operator==(const PortAddress& a, const PortAddress& b) { return a.node == b.node && a.port == b.port; }
		friend bool operator!=(const PortAddress& a, const PortAddress& b) { return !(a == b); }
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
	std::size_t operator()(const lain::flow::NodeId& id) const noexcept
	{
		return std::hash<lain::core::Uuid>{}(id.uuid());
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
	std::size_t operator()(const lain::flow::PortAddress& a) const noexcept
	{
		const std::size_t h = std::hash<lain::flow::NodeId>{}(a.node);
		return h ^ (std::hash<lain::flow::PortId>{}(a.port) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
	}
};
