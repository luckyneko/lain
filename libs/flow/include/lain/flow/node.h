#pragma once

#include "lain/flow/param.h"
#include "lain/flow/port.h"
#include "lain/flow/types.h"

#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace lain::flow
{
	class Graph; // a node may CONTAIN one (see innerGraph) — the group-node seam

	// Abstract base for a graph node. A subclass declares its ports in its
	// constructor (addInput / addOutput) and implements compute().
	//
	// Threading contract: compute() reads only this node's inputs and writes only
	// this node's outputs — no shared mutable state. That isolation is what lets
	// the scheduler run independent nodes on different threads. A `constant` node
	// clears dirty() after its first compute; an on-request source (e.g. a camera
	// capture) stays dirty so each pull refires it.
	class Node
	{
	public:
		virtual ~Node() = default;

		NodeId id() const { return m_id; }
		const std::string& name() const { return m_name; }

		// Rename the node — display only, like Port::setName. A node's identity is its NodeId: edges,
		// handles and the editor's layout all reference that, and nothing addresses a node by name (a
		// name need not even be unique — an adapter that wants uniqueness imposes it). Serialization
		// persists the name, so a user-chosen title survives a save/load round-trip.
		void setName(std::string name) { m_name = std::move(name); }

		PortIndex inputCount() const { return m_inputs.size(); }
		PortIndex outputCount() const { return m_outputs.size(); }
		Port& input(PortIndex i) { return m_inputs[i]; }
		const Port& input(PortIndex i) const { return m_inputs[i]; }
		Port& output(PortIndex i) { return m_outputs[i]; }
		const Port& output(PortIndex i) const { return m_outputs[i]; }

		// Resolve a port by its stable PortId (what edges reference), or nullptr if it is not
		// found — a linear scan (a node has few ports). Direction-scoped, so an edge's `from`
		// resolves through findOutput and its `to` through findInput.
		Port* findInput(PortId id) { return findPort(m_inputs, id); }
		const Port* findInput(PortId id) const { return findPort(m_inputs, id); }
		Port* findOutput(PortId id) { return findPort(m_outputs, id); }
		const Port* findOutput(PortId id) const { return findPort(m_outputs, id); }

		// Whether a port in `direction` already carries `name`. Backs the port-name-uniqueness
		// invariant that name-addressed serialization relies on: addInput/addOutput assert on a
		// duplicate (an author bug), addDynamicPort rejects one, and the editing layer guards a
		// boundary-pin rename with it. Uniqueness is per-direction (an input and an output may share
		// a name — an edge's from/to imply which).
		bool hasPortNamed(Port::Direction direction, const std::string& name) const
		{
			const std::vector<Port>& ports = (direction == Port::Direction::Input) ? m_inputs : m_outputs;
			for (const Port& port : ports)
			{
				if (port.name() == name)
					return true;
			}
			return false;
		}

		// Configuration values — distinct from ports (see Param). The adapter iterates
		// these to render editors and writes edits back; compute() reads them via
		// param(i).get<T>(). Non-connectable; the scheduler never touches them.
		PortIndex paramCount() const { return m_params.size(); }
		Param& param(PortIndex i) { return m_params[i]; }
		const Param& param(PortIndex i) const { return m_params[i]; }

		// Whether this node needs recomputing. VIRTUAL because a node that contains a graph (a group
		// node) is dirty when anything *inside* it is: otherwise an edit made inside a group would
		// never reach the outer dirty closure, and the group would keep serving a stale result.
		virtual bool dirty() const { return m_dirty; }
		void markDirty() { m_dirty = true; }
		void clearDirty() { m_dirty = false; }

		// This node's OWN dirty flag, ignoring anything it contains. The scheduler needs the two
		// apart: a group whose only dirt is inside it must be re-planned, but its inputs have not
		// changed, so republishing them into the inner boundary would needlessly dirty the whole
		// inner graph and destroy inner incrementality.
		bool selfDirty() const { return m_dirty; }

		// The graph this node CONTAINS, or nullptr for an ordinary node. The scheduler asks this of
		// every node while building its execution plan and expands the whole nesting tree into one
		// flat plan (ADR-0009) — so it asks the structural question it actually has ("do you contain
		// a graph?") rather than dynamic_cast-ing for a class identity it doesn't otherwise care
		// about, and a future graph-containing node needs no scheduler change.
		virtual Graph* innerGraph() { return nullptr; }
		const Graph* innerGraph() const { return const_cast<Node*>(this)->innerGraph(); }

		// The inner boundary pin that this node's port `outer` mirrors, or the null PortId.
		// Meaningful only alongside innerGraph(): the two together ARE the group seam the scheduler
		// drives — expand the contained graph, and know which inner pin each outer port crosses to.
		// Keeping it here (rather than casting to a concrete group class) is what lets a future
		// graph-containing node work with no scheduler change.
		virtual PortId innerPin(PortId /*outer*/) const { return PortId{}; }

		// Whether this node is READY to compute: every REQUIRED input carries a value (ADR-0007).
		// Node-local — it inspects only this node's own input ports. The scheduler gates compute() on
		// it (an unready node is skipped and its outputs cleared, suppressing downstream); a viewer
		// reads it post-run to tell which nodes activated (the dimmed ones did not).
		bool ready() const
		{
			for (const Port& in : m_inputs)
			{
				if (in.required() && in.value().empty())
					return false;
			}
			return true;
		}

		// The node's work: read inputs, write outputs. The scheduler calls this in
		// dependency order and clears dirty() around the call; an on-request source
		// can markDirty() itself here to refire on the next pull.
		virtual void compute() = 0;

	protected:
		explicit Node(std::string name)
			: m_name(std::move(name))
		{
		}

		// Declare a port in the subclass constructor; returns its index, for use
		// with input() / output() inside compute(). `presence` (input only) declares whether a value
		// on this input is Required for the node to be ready (the default) or Optional — an empty
		// Optional input does not suppress the node, for a Select/Merge branch its compute() checks
		// for presence and picks a live one (ADR-0007).
		template <typename T>
		PortIndex addInput(std::string name, Presence presence = Presence::Required);
		template <typename T>
		PortIndex addOutput(std::string name);

		// Declare a port MIRRORING an existing port's declared type, with no compile-time T. A
		// port's type is a shared PortType flyweight, so a node that DERIVES its interface from
		// another node's ports — a group mirroring its inner boundary pins — copies that flyweight
		// instead of needing the type. Which means it mirrors ANY type, including one no registry
		// knows about: a group's ports are derived, not user-chosen, so they must not be limited to
		// the registered set the way an addable dynamic pin is.
		PortIndex addInputLike(std::string name, const PortType& type, Presence presence = Presence::Required);
		PortIndex addOutputLike(std::string name, const PortType& type);

		// Declare a configuration param, seeded with `defaultValue`. Read it in compute()
		// via param(i).get<T>(); the adapter edits it via param(i).set<T>().
		template <typename T>
		PortIndex addParam(std::string name, T defaultValue);

	private:
		friend class Graph; // assigns the id when the node is added, and removes ports
		void setId(NodeId id) { m_id = id; }

		// Erase a port by id (raw — no edge check). Graph::removePort enforces the
		// no-dangling-edge invariant *before* calling this, so it is Graph-only. Returns whether
		// a port was found + erased; other ports keep their ids (the vector compacts).
		bool removePort(PortId id)
		{
			for (auto it = m_inputs.begin(); it != m_inputs.end(); ++it)
				if (it->id() == id)
				{
					m_inputs.erase(it);
					return true;
				}
			for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it)
				if (it->id() == id)
				{
					m_outputs.erase(it);
					return true;
				}
			return false;
		}

		// Mint the next stable PortId (addInput / addOutput call this). Defined in node.inl.
		PortId nextPortId();

		// Linear scan for a port by id (a node has few ports). Static so the const and non-const
		// find* share one body.
		template <typename Ports>
		static auto findPort(Ports& ports, PortId id) -> decltype(&ports[0])
		{
			for (auto& p : ports)
				if (p.id() == id)
					return &p;
			return nullptr;
		}

		NodeId m_id{};			// reserved sentinel until the Graph assigns a real id
		PortId m_nextPortId{1}; // per-node port id counter; 0 is the reserved sentinel
		std::string m_name;
		std::vector<Port> m_inputs;
		std::vector<Port> m_outputs;
		std::vector<Param> m_params;
		bool m_dirty = true;
	};
} // namespace lain::flow

#include "lain/flow/details/node.inl"
