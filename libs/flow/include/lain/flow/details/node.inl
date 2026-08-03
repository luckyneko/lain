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

	template <typename D>
	D* Node::checked(D* declared)
	{
		assert(declared != nullptr && "flow::Node: no port or param with that PortId on this node");
		return declared;
	}

	template <typename T>
	PortId Node::addInput(std::string name, Presence presence)
	{
		// An invalid or duplicate static-port name is an author bug (a bad name breaks cli/edge
		// addressing; a duplicate makes name-addressed edges ambiguous) — caught in debug. A runtime
		// pin, whose name may be user-supplied, rejects instead (addDynamicPort).
		assert(validPortName(name) && "flow::Node: port name must be a letter then alphanumeric/underscore");
		assert(!hasPortNamed(Port::Direction::Input, name) && "flow::Node: duplicate input port name");
		const PortId id = nextPortId();
		m_inputs.push_back(Port(std::move(name), Port::Direction::Input, portType<T>(), id, presence == Presence::Required));
		return id;
	}

	template <typename T>
	PortId Node::addOutput(std::string name)
	{
		assert(validPortName(name) && "flow::Node: port name must be a letter then alphanumeric/underscore");
		assert(!hasPortNamed(Port::Direction::Output, name) && "flow::Node: duplicate output port name");
		const PortId id = nextPortId();
		m_outputs.push_back(Port(std::move(name), Port::Direction::Output, portType<T>(), id));
		return id;
	}

	// The type-erased twins of addInput / addOutput (see node.h): same asserts, same id minting —
	// only the PortType comes from a caller-supplied flyweight rather than portType<T>().
	inline PortId Node::addInputLike(std::string name, const PortType& type, Presence presence)
	{
		assert(validPortName(name) && "flow::Node: port name must be a letter then alphanumeric/underscore");
		assert(!hasPortNamed(Port::Direction::Input, name) && "flow::Node: duplicate input port name");
		const PortId id = nextPortId();
		m_inputs.push_back(Port(std::move(name), Port::Direction::Input, type, id, presence == Presence::Required));
		return id;
	}

	inline PortId Node::addOutputLike(std::string name, const PortType& type)
	{
		assert(validPortName(name) && "flow::Node: port name must be a letter then alphanumeric/underscore");
		assert(!hasPortNamed(Port::Direction::Output, name) && "flow::Node: duplicate output port name");
		const PortId id = nextPortId();
		m_outputs.push_back(Port(std::move(name), Port::Direction::Output, type, id));
		return id;
	}

	inline bool Node::setParam(PortId id, PortValue value)
	{
		Param* param = findDeclared(m_params, id);
		if (param == nullptr)
			return false;
		// "The type is the schema": the param's DECLARED type is authoritative, so a value of any
		// other type (or none) is refused rather than quietly retyping the param.
		if (value.empty() || value.type() != param->type())
			return false;

		param->m_value = std::move(value); // Node is Param's friend — this is the only writer
		bumpVersion();					   // write and invalidate as ONE operation
		return true;
	}

	template <typename T>
	bool Node::setParam(PortId id, T value)
	{
		PortValue erased;
		erased.set<T>(std::move(value));
		return setParam(id, std::move(erased));
	}

	template <typename T>
	PortId Node::addParam(std::string name, T defaultValue)
	{
		// From the SAME counter the ports draw on, so no param id ever equals a port id on this
		// node — passing one to input() / output() finds nothing rather than the wrong thing.
		const PortId id = nextPortId();
		Param p(std::move(name), portType<T>(), id);
		p.set<T>(std::move(defaultValue));
		m_params.push_back(std::move(p));
		return id;
	}
} // namespace lain::flow
