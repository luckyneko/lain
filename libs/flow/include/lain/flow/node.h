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
		friend class Graph; // assigns the id when the node is added
		void setId(NodeId id) { m_id = id; }

		NodeId m_id{}; // reserved sentinel until the Graph assigns a real id
		std::string m_name;
		std::vector<Port> m_inputs;
		std::vector<Port> m_outputs;
		std::vector<Param> m_params;
		bool m_dirty = true;
	};
} // namespace lain::flow

#include "lain/flow/details/node.inl"
