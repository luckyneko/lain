#include "lain/flow/evaluation.h"

#include "lain/flow/boundary.h" // BoundaryInput / BoundaryOutput — the host binding handles
#include "lain/flow/graph.h"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace lain::flow
{
	// The empty value handed back for a port with no slot (an id from a stale handle, a port removed
	// since the last prepare). One shared instance: reading a value never allocates.
	static const PortValue& emptyValue()
	{
		static const PortValue empty;
		return empty;
	}

	//=========================================================================
	// NodeEvaluation — the per-node view
	//=========================================================================
	const PortValue& NodeEvaluation::input(PortId id) const
	{
		const Evaluation::NodeState* state = m_evaluation->state(m_id);
		if (state == nullptr)
			return emptyValue();
		const auto it = state->inputs.find(id);
		assert(it != state->inputs.end() && "flow::NodeEvaluation: no input with that PortId on this node");
		return it == state->inputs.end() ? emptyValue() : it->second;
	}

	const PortValue& NodeEvaluation::output(PortId id) const
	{
		const Evaluation::NodeState* state = m_evaluation->state(m_id);
		if (state == nullptr)
			return emptyValue();
		const auto it = state->outputs.find(id);
		assert(it != state->outputs.end() && "flow::NodeEvaluation: no output with that PortId on this node");
		return it == state->outputs.end() ? emptyValue() : it->second;
	}

	PortValue& NodeEvaluation::output(PortId id)
	{
		Evaluation::NodeState* state = m_evaluation->state(m_id);
		assert(state != nullptr && "flow::NodeEvaluation: this node was not prepared");
		const auto it = state->outputs.find(id);
		// Never inserts: prepare() created a slot for every declared port, which is what keeps a
		// worker task off shared-container growth.
		assert(it != state->outputs.end() && "flow::NodeEvaluation: no output with that PortId on this node");
		return it->second;
	}

	bool NodeEvaluation::ready() const
	{
		return m_evaluation->ready(m_id);
	}

	void NodeEvaluation::requestRecompute()
	{
		m_evaluation->requestRecompute(m_id);
	}

	//=========================================================================
	// Evaluation
	//=========================================================================
	Evaluation::Evaluation(const Graph& definition)
		: m_definition(&definition)
	{
		prepare(definition);
	}

	Evaluation::Evaluation(Evaluation&&) noexcept = default;
	Evaluation& Evaluation::operator=(Evaluation&&) noexcept = default;
	Evaluation::~Evaluation() = default;

	Evaluation::NodeState* Evaluation::state(NodeId id)
	{
		const auto it = m_nodes.find(id);
		return it == m_nodes.end() ? nullptr : &it->second;
	}

	const Evaluation::NodeState* Evaluation::state(NodeId id) const
	{
		const auto it = m_nodes.find(id);
		return it == m_nodes.end() ? nullptr : &it->second;
	}

	void Evaluation::prepareNode(NodeState& state, const Node& node)
	{
		// Ports that survived keep their values; ports that went take theirs with them. Rebuilt as
		// fresh maps rather than erased in place, so a renumbered port set costs one pass either way.
		std::map<PortId, PortValue> inputs;
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			const PortId id = node.input(i).id();
			const auto existing = state.inputs.find(id);
			inputs[id] = (existing == state.inputs.end()) ? PortValue{} : std::move(existing->second);
		}
		std::map<PortId, PortValue> outputs;
		for (std::size_t o = 0; o < node.outputCount(); ++o)
		{
			const PortId id = node.output(o).id();
			const auto existing = state.outputs.find(id);
			outputs[id] = (existing == state.outputs.end()) ? PortValue{} : std::move(existing->second);
		}
		state.inputs = std::move(inputs);
		state.outputs = std::move(outputs);
	}

	void Evaluation::prepare(const Graph& definition)
	{
		if (m_definition != nullptr && m_definition != &definition)
		{
			// See the header: this catches a mispaired call, not a rebuilt-in-place definition. The
			// real protection is that a host owns the two as one replaceable unit.
			throw std::logic_error("flow::Evaluation: prepared against a different Graph than it was built for");
		}
		m_definition = &definition;

		// Nodes: create what is new, keep what survived, drop what went (its values go with it).
		std::map<NodeId, NodeState> nodes;
		std::map<NodeId, std::vector<std::unique_ptr<Evaluation>>> children;
		for (const NodeId id : definition.nodeIds())
		{
			const Node& node = definition.node(id);
			const auto existing = m_nodes.find(id);
			NodeState state = (existing == m_nodes.end()) ? NodeState{} : std::move(existing->second);
			prepareNode(state, node);
			nodes.emplace(id, std::move(state));

			// Children of a graph-containing node, prepared recursively — so the whole tree is
			// stable before any task runs, at every level.
			//
			// prepare deliberately does NOT impose a COUNT. It keeps however many are already there
			// and guarantees at least one, because a map's count is data-dependent: it is set
			// between stages once the collection has been computed (ADR-0014), while prepare runs at
			// the top of every invocation. Forcing a count here would reset a map's N children — and
			// with them every element's retained values — on each run.
			if (const Graph* inner = node.innerGraph())
			{
				const auto existingChildren = m_children.find(id);
				std::vector<std::unique_ptr<Evaluation>> kept =
					(existingChildren == m_children.end())
						? std::vector<std::unique_ptr<Evaluation>>{}
						: std::move(existingChildren->second);
				// A group is always exactly one evaluation, so give it its one. A MAP's count is the
				// length of a collection this run has not computed yet, so leave it alone entirely —
				// including at zero, which is a legitimate answer (an empty collection) and must not
				// be quietly turned into one spurious child.
				if (kept.empty() && !node.evaluatesPerElement())
					kept.push_back(std::make_unique<Evaluation>());

				for (std::unique_ptr<Evaluation>& child : kept)
				{
					// A group whose inner Graph was REPLACED (a linked template re-resolved) gets a
					// fresh child: the old one's per-node versions belong to a definition that no
					// longer exists, which is the same mispairing prepare guards against above.
					if (child->m_definition != nullptr && child->m_definition != inner)
						child = std::make_unique<Evaluation>();
					child->prepare(*inner);
				}
				children.emplace(id, std::move(kept));
			}
		}
		m_nodes = std::move(nodes);
		m_children = std::move(children);
	}

	NodeEvaluation Evaluation::node(NodeId id)
	{
		assert(m_definition != nullptr && "flow::Evaluation: no definition (default-constructed)");
		assert(contains(id) && "flow::Evaluation: node was not prepared");
		return NodeEvaluation(*this, m_definition->node(id), id);
	}

	bool Evaluation::ready(NodeId id) const
	{
		const NodeState* state = this->state(id);
		if (state == nullptr || m_definition == nullptr)
			return false;

		// ADR-0007's gate, joining declaration with runtime: every REQUIRED input must carry a value.
		// An empty OPTIONAL input (a Select/Merge branch) does not block — compute() checks presence
		// itself and picks a live one.
		const Node& node = m_definition->node(id);
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			const Port& port = node.input(i);
			if (!port.required())
				continue;
			const auto it = state->inputs.find(port.id());
			if (it == state->inputs.end() || it->second.empty())
				return false;
		}
		return true;
	}

	bool Evaluation::hasValue(PortAddress port) const
	{
		return !value(port).empty();
	}

	const PortValue& Evaluation::value(PortAddress port) const
	{
		const NodeState* state = this->state(port.node);
		if (state == nullptr)
			return emptyValue();
		// A port is on one side or the other; look where it actually is rather than making the
		// caller say, since a PortId is unique across both sides of its node.
		const auto out = state->outputs.find(port.port);
		if (out != state->outputs.end())
			return out->second;
		const auto in = state->inputs.find(port.port);
		return in == state->inputs.end() ? emptyValue() : in->second;
	}

	std::string Evaluation::describe(PortAddress port) const
	{
		if (m_definition == nullptr || !m_definition->contains(port.node))
			return "(empty)";
		const Node& node = m_definition->node(port.node);
		const Port* declared = node.findOutput(port.port);
		if (declared == nullptr)
			declared = node.findInput(port.port);
		if (declared == nullptr)
			return "(empty)";
		// Rendering is the declared TYPE's capability, applied to this evaluation's value — so a new
		// payload type describes itself just by exposing toString(), with no central ladder.
		return declared->portType().describe(value(port));
	}

	bool Evaluation::needsRecompute(NodeId id) const
	{
		const NodeState* state = this->state(id);
		if (state == nullptr || m_definition == nullptr)
			return false;
		if (state->recomputeRequested)
			return true;
		return m_definition->node(id).version() != state->computedAt;
	}

	void Evaluation::requestRecompute(NodeId id)
	{
		if (NodeState* state = this->state(id))
			state->recomputeRequested = true;
	}

	void Evaluation::requestRecomputeAll()
	{
		for (auto& entry : m_nodes)
			entry.second.recomputeRequested = true;
		for (auto& entry : m_children)
		{
			for (std::unique_ptr<Evaluation>& child : entry.second)
				child->requestRecomputeAll();
		}
	}

	void Evaluation::bind(PortAddress input, PortValue value)
	{
		// A bound value IS the boundary pin's output value here: GroupInputNode computes nothing, it
		// carries what was supplied. Requesting its recompute is what pulls the new value into the
		// next run's closure.
		NodeState* state = this->state(input.node);
		if (state == nullptr)
			return;
		const auto it = state->outputs.find(input.port);
		if (it == state->outputs.end())
			return;
		it->second = std::move(value);
		state->recomputeRequested = true;
	}

	void Evaluation::bind(const BoundaryPin& input, PortValue value)
	{
		bind(input.port, std::move(value));
	}

	const PortValue& Evaluation::value(const BoundaryPin& output) const
	{
		return value(output.port);
	}

	PortValue& Evaluation::inputSlot(NodeId node, PortId port)
	{
		NodeState* state = this->state(node);
		assert(state != nullptr && "flow::Evaluation: node was not prepared");
		const auto it = state->inputs.find(port);
		assert(it != state->inputs.end() && "flow::Evaluation: no input with that PortId on this node");
		return it->second;
	}

	void Evaluation::clearRecomputeRequest(NodeId node)
	{
		if (NodeState* state = this->state(node))
			state->recomputeRequested = false;
	}

	void Evaluation::markComputed(NodeId node, std::uint64_t version)
	{
		if (NodeState* state = this->state(node))
			state->computedAt = version;
	}

	std::size_t Evaluation::childCount(NodeId group) const
	{
		const auto it = m_children.find(group);
		return it == m_children.end() ? 0 : it->second.size();
	}

	void Evaluation::setChildCount(NodeId node, std::size_t count)
	{
		std::vector<std::unique_ptr<Evaluation>>& children = m_children[node];
		if (count < children.size())
			children.resize(count); // dropping the tail drops those elements' retained values
		while (children.size() < count)
			children.push_back(std::make_unique<Evaluation>());
	}

	Evaluation& Evaluation::child(NodeId group, std::size_t index)
	{
		const auto it = m_children.find(group);
		assert(it != m_children.end() && "flow::Evaluation: no child Evaluation for that group node");
		assert(index < it->second.size() && "flow::Evaluation: no child Evaluation at that index");
		return *it->second[index];
	}

	const Evaluation& Evaluation::child(NodeId group, std::size_t index) const
	{
		const auto it = m_children.find(group);
		assert(it != m_children.end() && "flow::Evaluation: no child Evaluation for that group node");
		assert(index < it->second.size() && "flow::Evaluation: no child Evaluation at that index");
		return *it->second[index];
	}

	//=========================================================================
	// Evaluation::RunLease
	//=========================================================================
	Evaluation::RunLease::RunLease(Evaluation& evaluation)
		: m_evaluation(&evaluation)
	{
		if (evaluation.m_running)
		{
			m_evaluation = nullptr; // nothing to release — the destructor must not clear someone else's hold
			throw std::logic_error("flow::Evaluation: already being scheduled — one Evaluation runs once at a time");
		}
		evaluation.m_running = true;
	}

	Evaluation::RunLease::~RunLease()
	{
		if (m_evaluation != nullptr)
			m_evaluation->m_running = false;
	}
} // namespace lain::flow
