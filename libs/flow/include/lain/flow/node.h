#pragma once

#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <lain/flow/port.h>
#include <lain/flow/types.h>

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

		bool dirty() const { return m_dirty; }
		void markDirty() { m_dirty = true; }
		void clearDirty() { m_dirty = false; }

		// The node's work: read inputs, write outputs. The scheduler calls this in
		// dependency order and clears dirty() around the call; an on-request source
		// can markDirty() itself here to refire on the next pull.
		virtual void compute() = 0;

		// Optional ImGui inspector hook. Called on the main/render thread only.
		virtual void onInspect() {}

	protected:
		explicit Node(std::string name) : m_name(std::move(name)) {}

		// Declare a port in the subclass constructor; returns its index, for use
		// with input() / output() inside compute().
		template <typename T>
		PortIndex addInput(std::string name);
		template <typename T>
		PortIndex addOutput(std::string name);

	private:
		friend class Graph; // assigns the id when the node is added
		void setId(NodeId id) { m_id = id; }

		NodeId m_id = 0;
		std::string m_name;
		std::vector<Port> m_inputs;
		std::vector<Port> m_outputs;
		bool m_dirty = true;
	};
}

#include <lain/flow/details/node.inl>
