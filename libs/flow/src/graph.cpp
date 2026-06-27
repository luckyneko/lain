#include <lain/flow/graph.h>

#include <cstddef>

namespace lain::flow
{
	Connection Graph::connect(NodeId from, PortIndex outPort, NodeId to, PortIndex inPort)
	{
		if (!valid(from) || !valid(to))
			return Connection::InvalidNode;

		Node& source = *m_nodes[from];
		Node& target = *m_nodes[to];
		if (outPort >= source.outputCount() || inPort >= target.inputCount())
			return Connection::InvalidPort;

		if (source.output(outPort).type() != target.input(inPort).type())
			return Connection::TypeMismatch;

		for (const Edge& e : m_edges)
		{
			if (e.to == to && e.inPort == inPort)
				return Connection::InputInUse;
		}

		// Adding from -> to closes a cycle iff `from` is already reachable from `to`.
		if (from == to || reaches(to, from))
			return Connection::WouldCycle;

		m_edges.push_back(Edge{ from, outPort, to, inPort });
		m_topoValid = false;
		return Connection::Ok;
	}

	bool Graph::disconnect(NodeId to, PortIndex inPort)
	{
		for (auto it = m_edges.begin(); it != m_edges.end(); ++it)
		{
			if (it->to == to && it->inPort == inPort)
			{
				m_edges.erase(it);
				m_topoValid = false;
				return true;
			}
		}
		return false;
	}

	bool Graph::reaches(NodeId start, NodeId target) const
	{
		std::vector<NodeId> stack;
		stack.push_back(start);
		std::vector<bool> seen(m_nodes.size(), false);

		while (!stack.empty())
		{
			const NodeId n = stack.back();
			stack.pop_back();
			if (n == target)
				return true;
			if (seen[n])
				continue;
			seen[n] = true;

			for (const Edge& e : m_edges)
			{
				if (e.from == n && !seen[e.to])
					stack.push_back(e.to);
			}
		}
		return false;
	}

	const std::vector<NodeId>& Graph::topoOrder() const
	{
		if (m_topoValid)
			return m_topo;

		// Kahn's algorithm. The graph is acyclic by construction, so every node
		// drains and m_topo ends up covering all of them.
		std::vector<std::size_t> indegree(m_nodes.size(), 0);
		for (const Edge& e : m_edges)
			++indegree[e.to];

		std::vector<NodeId> ready;
		for (NodeId i = 0; i < m_nodes.size(); ++i)
		{
			if (indegree[i] == 0)
				ready.push_back(i);
		}

		m_topo.clear();
		m_topo.reserve(m_nodes.size());
		while (!ready.empty())
		{
			const NodeId n = ready.back();
			ready.pop_back();
			m_topo.push_back(n);

			for (const Edge& e : m_edges)
			{
				if (e.from == n && --indegree[e.to] == 0)
					ready.push_back(e.to);
			}
		}

		m_topoValid = true;
		return m_topo;
	}
}
