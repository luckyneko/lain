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
				if (port.name() == name)
					return true;
			return false;
		}

		// Configuration values — distinct from ports (see Param). The adapter iterates
		// these to render editors and writes edits back; compute() reads them via
		// param(i).get<T>(). Non-connectable; the scheduler never touches them.
		PortIndex paramCount() const { return m_params.size(); }
		Param& param(PortIndex i) { return m_params[i]; }
		const Param& param(PortIndex i) const { return m_params[i]; }

		bool dirty() const { return m_dirty; }
		void markDirty() { m_dirty = true; }
		void clearDirty() { m_dirty = false; }

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
		// with input() / output() inside compute().
		template <typename T>
		PortIndex addInput(std::string name);
		template <typename T>
		PortIndex addOutput(std::string name);

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
