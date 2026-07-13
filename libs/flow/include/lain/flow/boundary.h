#pragma once

// The graph's I/O boundary — how a graph exposes an interface a host binds, without
// load/save *nodes*. A GroupInputNode publishes host-supplied values into the graph; a
// GroupOutputNode exposes values for the host to read. Each is a single node with *one or
// more pins* (Blender's Group Input / Group Output), so a graph's whole interface is two
// nodes, not 2N. flow's ports are individually typed (addOutput<T> per pin), so a node
// declares a fixed set of differently-typed pins at construction with no dynamic ports —
// runtime add/remove of pins is vertical b, and reuses these same nodes.
//
// The seam is *pin-centric*: a bindable input/output is a named, typed PIN (a BoundaryInput
// / BoundaryOutput handle), and Graph::boundaryInputs / boundaryOutputs flatten every
// boundary node's pins into one list — so a host binds inputs without caring how many nodes
// host them. All crossing is through the type-erased PortValue; flow core names no payload
// type.

#include "lain/flow/dynamicports.h" // DynamicPortsNode — boundary nodes have dynamic pins
#include "lain/flow/node.h"
#include "lain/flow/portvalue.h"
#include "lain/flow/types.h" // PortId

#include <map>
#include <string>
#include <typeindex>
#include <utility>

namespace lain::flow
{
	// A graph INPUT boundary: one or more host-bound output pins. The host injects each pin's
	// value (setValue); compute() publishes it to that pin. Add pins at construction with
	// addBoundary<T> — each is an ordinary typed output port, so pins may differ in type.
	class GroupInputNode : public DynamicPortsNode
	{
	public:
		GroupInputNode()
			: DynamicPortsNode("GroupInput")
		{
		}

		// A GroupInput's graph-inputs are its OUTPUT pins, so that's the side that grows.
		Port::Direction dynamicSide() const override { return Port::Direction::Output; }

		// Declare a bindable input pin of type T; returns its stable PortId. Routes through
		// addDynamicPort so a construction-time pin and a runtime-added pin are the same path
		// (both get a bound slot via onDynamicPortAdded).
		template <typename T>
		PortId addBoundary(std::string name)
		{
			return addDynamicPort<T>(std::move(name));
		}

		PortIndex boundaryCount() const { return outputCount(); }

		// Inject the value for `pin`; published to that output on the next compute(). Marks
		// dirty so a pull re-fires it, after which it stays constant until the next setValue (a
		// live input in vertical b simply stays dirty). The payload type must match the pin's.
		void setValue(PortId pin, PortValue value)
		{
			m_bound[pin] = std::move(value);
			markDirty();
		}

		// Publish each pin's host-injected value (a type-erased copy) to its output port.
		void compute() override
		{
			for (const auto& [id, value] : m_bound)
				if (Port* p = findOutput(id))
					p->value() = value;
		}

	protected:
		// A new pin (construction-time or runtime) gets an empty bound slot to inject into later.
		void onDynamicPortAdded(PortId id) override { m_bound[id]; }

	private:
		std::map<PortId, PortValue> m_bound; // per-pin host-injected value, republished each compute()
	};

	// A graph OUTPUT boundary: one or more pins whose delivered value the host reads after a
	// run. Add pins at construction with addBoundary<T>; compute() is a passthrough.
	class GroupOutputNode : public DynamicPortsNode
	{
	public:
		GroupOutputNode()
			: DynamicPortsNode("GroupOutput")
		{
		}

		// A GroupOutput's graph-outputs are its INPUT pins, so that's the side that grows.
		Port::Direction dynamicSide() const override { return Port::Direction::Input; }

		// Declare a graph-output pin of type T; returns its stable PortId.
		template <typename T>
		PortId addBoundary(std::string name)
		{
			return addDynamicPort<T>(std::move(name));
		}

		PortIndex boundaryCount() const { return inputCount(); }

		// The value delivered to `pin` (its input's current value). Empty until the graph has
		// run with a producer wired in (or if `pin` is unknown).
		const PortValue& value(PortId pin) const
		{
			static const PortValue empty;
			const Port* p = findInput(pin);
			return p ? p->value() : empty;
		}

		void compute() override {} // passthrough — the scheduler populated the inputs
	};

	// A bindable graph input: a named, typed pin (addressed by its stable PortId) on a
	// GroupInputNode. The host binds through this (name / type / setValue) without knowing which
	// node — or how many — host the pins, and it survives sibling pins being added/removed.
	struct BoundaryInput
	{
		GroupInputNode* node;
		PortId pin;

		const std::string& name() const { return node->findOutput(pin)->name(); }
		std::string_view typeName() const { return node->findOutput(pin)->typeName(); }
		std::type_index type() const { return node->findOutput(pin)->type(); }
		void setValue(PortValue value) const { node->setValue(pin, std::move(value)); }
	};

	// A readable graph output: a named, typed pin (by stable PortId) on a GroupOutputNode.
	struct BoundaryOutput
	{
		GroupOutputNode* node;
		PortId pin;

		const std::string& name() const { return node->findInput(pin)->name(); }
		std::string_view typeName() const { return node->findInput(pin)->typeName(); }
		std::type_index type() const { return node->findInput(pin)->type(); }
		const PortValue& value() const { return node->value(pin); }

		// The delivered value as human-readable text (Port::describe — the shared meta::toString
		// pathway): "(empty)" when nothing was produced, else the value. A host writes this to capture
		// a scalar/text output the way it saves an image output to a file.
		std::string describe() const { return node->findInput(pin)->describe(); }
	};
} // namespace lain::flow
