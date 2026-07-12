#include "lain/flow/graph.h" // includes boundary.h (GroupInput/OutputNode, BoundaryInput/Output)

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <utility>

namespace lain::flow
{
	NodeId Graph::add(std::unique_ptr<Node> node)
	{
		const NodeId id = m_nextId;
		m_nextId = NodeId{m_nextId.value() + 1};
		node->setId(id); // Graph is a friend of Node
		m_nodes.emplace(id, std::move(node));
		m_topoValid = false;
		return id;
	}

	std::vector<NodeId> Graph::nodeIds() const
	{
		std::vector<NodeId> ids;
		ids.reserve(m_nodes.size());
		for (const auto& entry : m_nodes) // m_nodes is ordered by NodeId, so this is id-ascending
			ids.push_back(entry.first);
		return ids;
	}

	bool Graph::removeNode(NodeId id)
	{
		const auto it = m_nodes.find(id);
		if (it == m_nodes.end())
			return false;

		// Drop every edge that touches the node, in either direction. A downstream
		// input keeps its last-copied value (as with disconnect) until re-evaluated.
		m_edges.erase(std::remove_if(m_edges.begin(), m_edges.end(),
									 [id](const Edge& e)
									 { return e.from.node == id || e.to.node == id; }),
					  m_edges.end());
		m_nodes.erase(it);
		m_topoValid = false;
		return true;
	}

	Connection Graph::connect(PortAddress from, PortAddress to)
	{
		if (!valid(from.node) || !valid(to.node))
			return Connection::InvalidNode;

		const Port* out = node(from.node).findOutput(from.port);
		const Port* in = node(to.node).findInput(to.port);
		if (out == nullptr || in == nullptr)
			return Connection::InvalidPort;

		if (out->type() != in->type())
			return Connection::TypeMismatch;

		for (const Edge& e : m_edges)
		{
			if (e.to == to)
				return Connection::InputInUse;
		}

		// Adding from -> to closes a cycle iff `from` is already reachable from `to`.
		if (from.node == to.node || reaches(to.node, from.node))
			return Connection::WouldCycle;

		m_edges.push_back(Edge{from, to});
		m_topoValid = false;
		return Connection::Ok;
	}

	Connection Graph::connect(NodeId from, PortIndex outPort, NodeId to, PortIndex inPort)
	{
		if (!valid(from) || !valid(to))
			return Connection::InvalidNode;

		Node& source = node(from);
		Node& target = node(to);
		if (outPort >= source.outputCount() || inPort >= target.inputCount())
			return Connection::InvalidPort;

		// Resolve the positions to stable addresses now; the edge stores the ids, not the indices.
		return connect(PortAddress{from, source.output(outPort).id()}, PortAddress{to, target.input(inPort).id()});
	}

	bool Graph::disconnect(PortAddress input)
	{
		for (auto it = m_edges.begin(); it != m_edges.end(); ++it)
		{
			if (it->to == input)
			{
				m_edges.erase(it);
				m_topoValid = false;
				return true;
			}
		}
		return false;
	}

	bool Graph::disconnect(NodeId to, PortIndex inPort)
	{
		if (!valid(to))
			return false;
		Node& target = node(to);
		if (inPort >= target.inputCount())
			return false;
		return disconnect(PortAddress{to, target.input(inPort).id()});
	}

	bool Graph::removePort(PortAddress port)
	{
		if (!valid(port.node))
			return false;

		// Refuse while any edge still touches the port (as a source feeding downstream, or as its
		// own input) — removing it would orphan that edge. edit::removePort clears them first.
		for (const Edge& e : m_edges)
		{
			if (e.from == port || e.to == port)
				return false;
		}
		return node(port.node).removePort(port.port);
	}

	const std::vector<NodeId>& Graph::topoOrder() const
	{
		if (m_topoValid)
			return m_topo;

		// Kahn's algorithm over the id-keyed node set (ids aren't contiguous, so
		// indegree is a map, not a vector). Acyclic by construction, so every node
		// drains and m_topo ends up covering all of them.
		std::map<NodeId, std::size_t> indegree;
		for (const auto& entry : m_nodes)
			indegree[entry.first] = 0;
		for (const Edge& e : m_edges)
			++indegree[e.to.node];

		std::vector<NodeId> ready;
		for (const auto& entry : indegree) // ascending id -> deterministic seeding
		{
			if (entry.second == 0)
				ready.push_back(entry.first);
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
				if (e.from.node == n && --indegree[e.to.node] == 0)
					ready.push_back(e.to.node);
			}
		}

		m_topoValid = true;
		return m_topo;
	}

	bool Graph::reaches(NodeId start, NodeId target) const
	{
		std::vector<NodeId> stack;
		stack.push_back(start);
		std::set<NodeId> seen;

		while (!stack.empty())
		{
			const NodeId n = stack.back();
			stack.pop_back();
			if (n == target)
				return true;
			if (seen.count(n) != 0)
				continue;
			seen.insert(n);

			for (const Edge& e : m_edges)
			{
				if (e.from.node == n && seen.count(e.to.node) == 0)
					stack.push_back(e.to.node);
			}
		}
		return false;
	}

	std::vector<BoundaryInput> Graph::boundaryInputs()
	{
		std::vector<BoundaryInput> out;
		for (auto& entry : m_nodes)
		{
			if (auto* node = dynamic_cast<GroupInputNode*>(entry.second.get()))
			{
				for (PortIndex i = 0; i < node->outputCount(); ++i) // a GroupInput's outputs are the graph's inputs
					out.push_back(BoundaryInput{node, node->output(i).id()});
			}
		}
		return out;
	}

	std::vector<BoundaryOutput> Graph::boundaryOutputs()
	{
		std::vector<BoundaryOutput> out;
		for (auto& entry : m_nodes)
		{
			if (auto* node = dynamic_cast<GroupOutputNode*>(entry.second.get()))
			{
				for (PortIndex i = 0; i < node->inputCount(); ++i) // a GroupOutput's inputs are the graph's outputs
					out.push_back(BoundaryOutput{node, node->input(i).id()});
			}
		}
		return out;
	}

	GroupInputNode* Graph::boundaryInputNode()
	{
		for (auto& entry : m_nodes)
			if (auto* node = dynamic_cast<GroupInputNode*>(entry.second.get()))
				return node;
		return nullptr;
	}

	GroupOutputNode* Graph::boundaryOutputNode()
	{
		for (auto& entry : m_nodes)
			if (auto* node = dynamic_cast<GroupOutputNode*>(entry.second.get()))
				return node;
		return nullptr;
	}
} // namespace lain::flow
