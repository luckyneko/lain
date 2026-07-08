#pragma once

// Template method definitions for lain::flow::Node (see node.h). The Port is built
// here in Node's scope (Node is its friend), then moved into the port list.

namespace lain::flow
{
	template <typename T>
	PortIndex Node::addInput(std::string name)
	{
		m_inputs.push_back(Port(std::move(name), Port::Direction::Input, portType<T>()));
		return m_inputs.size() - 1;
	}

	template <typename T>
	PortIndex Node::addOutput(std::string name)
	{
		m_outputs.push_back(Port(std::move(name), Port::Direction::Output, portType<T>()));
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
