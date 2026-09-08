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
	PortId Node::addInput(std::string name, Default<T> fallback)
	{
		// REQUIRED, deliberately: see Node::addInput's contract. A defaulted input that were Optional
		// would let a connected-but-suppressed upstream reach compute() as an empty slot, and the
		// node would read it expecting the default to be there.
		const PortId port = addInput<T>(name, Presence::Required);
		// The param carries the same NAME as the port. They live in separate namespaces (params are
		// serialized in their own array, ports are addressed per direction), and sharing the name is
		// what makes an existing document — written when these were two hand-declared halves — load
		// unchanged.
		const PortId param = addParam<T>(std::move(name), std::move(fallback.value));
		m_defaults[port] = param;
		return port;
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

	inline bool Node::renamePort(PortId port, std::string name)
	{
		Port* declared = findDeclared(m_inputs, port);
		if (declared == nullptr)
			declared = findDeclared(m_outputs, port);
		if (declared == nullptr)
			return false;
		if (!validPortName(name) || (name != declared->name() && hasPortNamed(declared->direction(), name)))
			return false;

		// The param behind a DEFAULTED input carries the port's name, and is found by that name on
		// load — so the two move together or the default is lost on the next round trip.
		if (const auto it = m_defaults.find(port); it != m_defaults.end())
		{
			if (Param* fallback = findDeclared(m_params, it->second))
				fallback->rename(name); // Node is Param's friend
		}
		declared->setName(std::move(name));
		return true;
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

	inline const PortType* Node::payloadType(const std::string& name) const
	{
		for (const PayloadType& payload : m_payloadTypes)
		{
			if (payload.name == name)
				return payload.type;
		}
		return nullptr;
	}

	inline void Node::addPayloadType(std::string name, const PortType& initial)
	{
		assert(payloadType(name) == nullptr && "flow::Node: duplicate payload type name");
		m_payloadTypes.push_back(PayloadType{std::move(name), &initial});
	}

	// The index of the payload type `name`, asserted to exist: a node declares from its OWN payload
	// types, so naming one it never declared is an author bug, like reaching input(PortId) with an
	// id from another node.
	inline std::size_t Node::payloadIndex(const std::string& name) const
	{
		for (std::size_t i = 0; i < m_payloadTypes.size(); ++i)
		{
			if (m_payloadTypes[i].name == name)
				return i;
		}
		assert(false && "flow::Node: no payload type by that name — declare it with addPayloadType first");
		return 0;
	}

	inline PortId Node::addInputOf(const std::string& payload, std::string name, Presence presence)
	{
		const std::size_t slot = payloadIndex(payload);
		const PortId id = addInputLike(std::move(name), *m_payloadTypes[slot].type, presence);
		m_payloadOf[id] = slot;
		return id;
	}

	inline PortId Node::addOutputOf(const std::string& payload, std::string name)
	{
		const std::size_t slot = payloadIndex(payload);
		const PortId id = addOutputLike(std::move(name), *m_payloadTypes[slot].type);
		m_payloadOf[id] = slot;
		return id;
	}

	inline PortId Node::addParamOf(const std::string& payload, std::string name)
	{
		const std::size_t slot = payloadIndex(payload);
		const PortType& type = *m_payloadTypes[slot].type;
		const PortId id = nextPortId();
		Param p(std::move(name), type, id);
		// A type with no default simply starts empty — the same state a param whose old value no
		// conversion reaches ends in, so there is one shape to handle rather than two.
		if (type.defaultValue != nullptr)
			p.m_value = type.defaultValue();
		m_params.push_back(std::move(p));
		m_payloadOf[id] = slot;
		return id;
	}

	inline void Node::tagAs(PortId declaration, const std::string& payload)
	{
		const std::size_t slot = payloadIndex(payload);
		const PortType& type = *m_payloadTypes[slot].type;

		const Port* port = findDeclared(m_inputs, declaration);
		if (port == nullptr)
			port = findDeclared(m_outputs, declaration);
		if (port != nullptr)
		{
			if (port->type() == type.index)
				m_payloadOf[declaration] = slot;
			return;
		}
		if (const Param* param = findDeclared(m_params, declaration); param != nullptr && param->type() == type.index)
			m_payloadOf[declaration] = slot;
	}

	inline PortId Node::addDefaultedInputOf(const std::string& payload, std::string name)
	{
		// REQUIRED and same-named as its param, for the reasons addInput<T>(name, Default{...})
		// gives — this is that declaration with the type coming from a payload type instead of a T.
		const PortId port = addInputOf(payload, name, Presence::Required);
		const PortId param = addParamOf(payload, std::move(name));
		m_defaults[port] = param;
		return port;
	}

	inline std::vector<PortId> Node::declarationsOf(const std::string& name) const
	{
		std::vector<PortId> declared;
		for (std::size_t i = 0; i < m_payloadTypes.size(); ++i)
		{
			if (m_payloadTypes[i].name != name)
				continue;
			for (const auto& tagged : m_payloadOf)
			{
				if (tagged.second == i)
					declared.push_back(tagged.first);
			}
			break;
		}
		return declared;
	}

	inline bool Node::retypePayload(const std::string& name, const PortType& type, std::vector<PortId>* reset)
	{
		std::size_t slot = 0;
		bool found = false;
		for (std::size_t i = 0; i < m_payloadTypes.size(); ++i)
		{
			if (m_payloadTypes[i].name == name)
			{
				slot = i;
				found = true;
				break;
			}
		}
		if (!found || !acceptsPayloadType(name, type))
			return false;
		if (m_payloadTypes[slot].type == &type)
			return true; // already this type — nothing declared differently, so nothing to invalidate

		m_payloadTypes[slot].type = &type;
		for (const auto& tagged : m_payloadOf)
		{
			if (tagged.second != slot)
				continue;

			// IN PLACE: the declaration keeps its id, name, order and presence, so an edge that
			// still typechecks keeps pointing at the same port.
			if (Port* in = findDeclared(m_inputs, tagged.first))
				in->m_type = &type;
			else if (Port* out = findDeclared(m_outputs, tagged.first))
				out->m_type = &type;
			else if (Param* param = findDeclared(m_params, tagged.first))
			{
				param->m_type = &type;
				param->m_value = (type.defaultValue != nullptr) ? type.defaultValue() : PortValue{};
				if (reset != nullptr)
					reset->push_back(tagged.first);
			}
		}
		bumpVersion(); // the node declares something different — every evaluation of it is stale
		return true;
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
