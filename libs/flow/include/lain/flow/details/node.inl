#pragma once

// Template method definitions for lain::flow::Node (see node.h). The Port is built
// here in Node's scope (Node is its friend), then moved into the port list.

#include <cassert>

namespace lain::flow
{
	// Mint the next stable PortId for a port this node is declaring.
	inline PortId Node::nextPortId()
	{
		const PortId id = m_nextPortId;
		m_nextPortId = PortId{m_nextPortId.value() + 1};
		return id;
	}

	template <typename T>
	PortIndex Node::addInput(std::string name, Presence presence)
	{
		// An invalid or duplicate static-port name is an author bug (a bad name breaks cli/edge
		// addressing; a duplicate makes name-addressed edges ambiguous) — caught in debug. A runtime
		// pin, whose name may be user-supplied, rejects instead (addDynamicPort).
		assert(validPortName(name) && "flow::Node: port name must be a letter then alphanumeric/underscore");
		assert(!hasPortNamed(Port::Direction::Input, name) && "flow::Node: duplicate input port name");
		m_inputs.push_back(Port(std::move(name), Port::Direction::Input, portType<T>(), nextPortId(), presence == Presence::Required));
		return m_inputs.size() - 1;
	}

	template <typename T>
	PortIndex Node::addOutput(std::string name)
	{
		assert(validPortName(name) && "flow::Node: port name must be a letter then alphanumeric/underscore");
		assert(!hasPortNamed(Port::Direction::Output, name) && "flow::Node: duplicate output port name");
		m_outputs.push_back(Port(std::move(name), Port::Direction::Output, portType<T>(), nextPortId()));
		return m_outputs.size() - 1;
	}

	template <typename T>
	PortIndex Node::addParam(std::string name, T defaultValue)
	{
		Param p(std::move(name), portType<T>());
		p.set<T>(std::move(defaultValue));
		m_params.push_back(std::move(p));
		return m_params.size() - 1;
	}
} // namespace lain::flow
