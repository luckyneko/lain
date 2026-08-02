#include "lain/flow/graph.h" // includes boundary.h (GroupInput/OutputNode, BoundaryInput/Output)

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <utility>

namespace lain::flow
{
	Graph::Graph()
		: Graph(BoundaryIds{}) // both null -> both minted
	{
	}

	Graph::Graph(BoundaryIds boundary)
	{
		// The interface pair (see graph.h). Seated through the private adopt path, not add(), which
		// refuses exactly these types once the pair exists. usableId mints what the caller did not
		// supply, so a loader's missing / duplicated boundary id costs the pair nothing.
		m_boundaryIn = adopt(std::make_unique<GroupInputNode>(), usableId(boundary.input));
		m_boundaryOut = adopt(std::make_unique<GroupOutputNode>(), usableId(boundary.output));
	}

	NodeId Graph::usableId(NodeId requested) const
	{
		if (requested != NodeId{} && m_nodes.count(requested) == 0)
			return requested;
		// Mint. The loop is for completeness rather than for a case anyone will hit — a fresh uuid
		// colliding with one of this graph's is ~10^-8 at best — but it makes admission total.
		NodeId minted = NodeId::generate();
		while (m_nodes.count(minted) != 0)
			minted = NodeId::generate();
		return minted;
	}

	NodeId Graph::adopt(std::unique_ptr<Node> node, NodeId id)
	{
		node->setId(id); // Graph is a friend of Node
		m_nodes.emplace(id, std::move(node));
		m_order.push_back(id); // lookup order and node order are separate — see nodeIds()
		m_topoValid = false;
		return id;
	}

	NodeId Graph::add(std::unique_ptr<Node> node)
	{
		return add(std::move(node), NodeId{}); // null requested id -> minted
	}

	NodeId Graph::add(std::unique_ptr<Node> node, NodeId requestedId)
	{
		if (!node)
			return NodeId{};

		// The graph already owns its interface pair, so a second of either is refused rather than
		// silently accepted and ignored by boundaryInputNode() / boundaryOutputNode(). A loader
		// rebuilding a saved graph adopts the existing pair instead of adding the document's.
		if (dynamic_cast<const GroupInputNode*>(node.get()) != nullptr && m_boundaryIn != NodeId{})
			return NodeId{};
		if (dynamic_cast<const GroupOutputNode*>(node.get()) != nullptr && m_boundaryOut != NodeId{})
			return NodeId{};

		return adopt(std::move(node), usableId(requestedId));
	}

	bool Graph::removeNode(NodeId id)
	{
		// The interface pair is not removable — see the Graph constructor. Refusing here (rather
		// than in the editing layer) keeps the invariant total: no gesture, loader or paste path can
		// leave a graph without a way in or out.
		if (isBoundary(id))
			return false;

		const auto it = m_nodes.find(id);
		if (it == m_nodes.end())
			return false;

		// A downstream consumer loses an input source when this node goes — mark each dirty before
		// the edges are dropped, so incremental re-eval recomputes them.
		for (const Edge& e : m_edges)
		{
			if (e.from.node == id && e.to.node != id)
				node(e.to.node).markDirty();
		}

		// Drop every edge that touches the node, in either direction. A downstream
		// input keeps its last-copied value (as with disconnect) until re-evaluated.
		m_edges.erase(std::remove_if(m_edges.begin(), m_edges.end(),
									 [id](const Edge& e)
									 { return e.from.node == id || e.to.node == id; }),
					  m_edges.end());
		m_nodes.erase(it);
		m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());
		m_topoValid = false;
		return true;
	}

	void Graph::markAllDirty()
	{
		for (auto& entry : m_nodes)
			entry.second->markDirty();
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
		node(to.node).markDirty(); // the downstream node gained an input source — recompute it
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
				node(input.node).markDirty(); // lost its source — recompute (input now empty)
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
		if (!node(port.node).removePort(port.port))
			return false;
		node(port.node).markDirty(); // the node's port set changed — recompute
		return true;
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

		// Seed from INSERTION order, not from the map: a uuid's order is arbitrary, and topo order
		// is not cosmetic — it drives serial execution and the canvas's default column layout, so
		// the same graph must produce the same order every time it is built.
		std::vector<NodeId> ready;
		for (const NodeId id : m_order)
		{
			if (indegree[id] == 0)
				ready.push_back(id);
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

	// The pin lists and the node accessors all resolve through the invariant pair, so the RTTI scan
	// these used to do is gone: there is exactly one node of each kind, and its id is known.
	std::vector<BoundaryInput> Graph::boundaryInputs()
	{
		GroupInputNode& node = boundaryInputNode();
		std::vector<BoundaryInput> out;
		for (PortIndex i = 0; i < node.outputCount(); ++i) // a GroupInput's outputs are the graph's inputs
			out.push_back(BoundaryInput{&node, node.output(i).id()});
		return out;
	}

	std::vector<BoundaryOutput> Graph::boundaryOutputs()
	{
		GroupOutputNode& node = boundaryOutputNode();
		std::vector<BoundaryOutput> out;
		for (PortIndex i = 0; i < node.inputCount(); ++i) // a GroupOutput's inputs are the graph's outputs
			out.push_back(BoundaryOutput{&node, node.input(i).id()});
		return out;
	}

	GroupInputNode& Graph::boundaryInputNode()
	{
		return static_cast<GroupInputNode&>(node(m_boundaryIn));
	}

	GroupOutputNode& Graph::boundaryOutputNode()
	{
		return static_cast<GroupOutputNode&>(node(m_boundaryOut));
	}
} // namespace lain::flow
