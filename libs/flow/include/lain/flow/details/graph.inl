#pragma once

// Template method definitions for lain::flow::Graph (see graph.h).

namespace lain::flow
{
	template <typename T, typename... Args>
	NodeId Graph::add(Args&&... args)
	{
		static_assert(std::is_base_of<Node, T>::value, "T must derive from lain::flow::Node");
		const NodeId id = m_nextId;
		m_nextId = NodeId{m_nextId.value() + 1};
		auto created = std::make_unique<T>(std::forward<Args>(args)...);
		created->setId(id); // Graph is a friend of Node
		m_nodes.emplace(id, std::move(created));
		m_topoValid = false;
		return id;
	}
} // namespace lain::flow
