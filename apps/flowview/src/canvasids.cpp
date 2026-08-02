#include "canvasids.h"

#include <utility>

namespace flowview
{
	using namespace lain;

	// Look `key` up in `forward`, allocating the next canvas int (and the reverse entry) if it is
	// not there yet. The three public accessors differ only in which pair of maps they use.
	template <typename Key, typename Reverse>
	static int intern(std::unordered_map<Key, int>& forward, Reverse& reverse, int& next, const Key& key, typename Reverse::mapped_type value)
	{
		const auto it = forward.find(key);
		if (it != forward.end())
			return it->second;
		const int canvasId = next++;
		forward.emplace(key, canvasId);
		reverse.emplace(canvasId, std::move(value));
		return canvasId;
	}

	int CanvasIds::node(flow::NodeId id)
	{
		return intern(m_nodes, m_nodesByCanvasId, m_next, id, id);
	}

	int CanvasIds::pin(flow::PortAddress address, bool output)
	{
		auto& forward = output ? m_outputPins : m_inputPins;
		return intern(forward, m_pinsByCanvasId, m_next, address, Pin{address, output});
	}

	int CanvasIds::link(flow::PortAddress destination)
	{
		return intern(m_links, m_linksByCanvasId, m_next, destination, destination);
	}

	std::optional<flow::NodeId> CanvasIds::toNode(int canvasId) const
	{
		const auto it = m_nodesByCanvasId.find(canvasId);
		if (it == m_nodesByCanvasId.end())
			return std::nullopt;
		return it->second;
	}

	std::optional<CanvasIds::Pin> CanvasIds::toPin(int canvasId) const
	{
		const auto it = m_pinsByCanvasId.find(canvasId);
		if (it == m_pinsByCanvasId.end())
			return std::nullopt;
		return it->second;
	}

	std::optional<flow::PortAddress> CanvasIds::toLink(int canvasId) const
	{
		const auto it = m_linksByCanvasId.find(canvasId);
		if (it == m_linksByCanvasId.end())
			return std::nullopt;
		return it->second;
	}

	void CanvasIds::reset()
	{
		m_nodes.clear();
		m_nodesByCanvasId.clear();
		m_inputPins.clear();
		m_outputPins.clear();
		m_pinsByCanvasId.clear();
		m_links.clear();
		m_linksByCanvasId.clear();
		// The counter restarts too: nothing from the old document remains to collide with, and
		// imnodes' own state for those ints is stale either way (the canvas re-seeds on a swap).
		m_next = 1;
	}
} // namespace flowview
