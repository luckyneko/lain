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
// type. The VALUES themselves live in the Evaluation, not on these nodes (ADR-0012) — which is what
// lets one definition serve several differently-bound evaluations at once.

#include "lain/flow/dynamicports.h" // DynamicPortsNode — boundary nodes have dynamic pins
#include "lain/flow/node.h"
#include "lain/flow/types.h" // PortId / PortAddress

#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

namespace lain::flow
{
	// A graph INPUT boundary: one or more host-bound output pins. The host supplies each pin's value
	// through the Evaluation (evaluation.bind); this node declares the pins and carries nothing.
	// Add pins with addBoundary<T> — each is an ordinary typed output port, so pins may differ in type.
	class GroupInputNode : public DynamicPortsNode
	{
	public:
		GroupInputNode()
			: DynamicPortsNode("GroupInput")
		{
		}

		// A GroupInput's graph-inputs are its OUTPUT pins, so that's the side that grows.
		Port::Direction dynamicSide() const override { return Port::Direction::Output; }

		// Declare a bindable input pin of type T; returns its stable PortId.
		template <typename T>
		PortId addBoundary(std::string name)
		{
			return addDynamicPort<T>(std::move(name));
		}

		// Declare a RESERVED pin: one the ENGINE writes rather than one a user adds — declared by the
		// node that OWNS this graph, for its own interior (ADR-0021's `index`, which tells a loop body
		// which iteration it is in). Returns its stable PortId.
		//
		// STATIC, deliberately — not a dynamic pin. Serialization replays only dynamic pins, so a
		// reserved pin is rebuilt on load by the owner's constructor, exactly as a Select's
		// ctor-declared `selector` is (Port::isDynamic). An edge to it is name-addressed on disk like
		// any other, so it resolves onto the ctor-created pin with nothing special in the loader.
		template <typename T>
		PortId addReserved(std::string name)
		{
			return addOutput<T>(std::move(name));
		}

		std::size_t boundaryCount() const { return outputCount(); }

		// Nothing to compute: a graph input's value is BOUND, not produced, and a bound value is
		// runtime state — so it lives in the Evaluation, as this pin's output value there
		// (Evaluation::bind writes it). The node is still a real step in the plan, because
		// everything downstream depends on it and its recompute request is what carries a rebind
		// into the run; it simply carries what the host supplied. (ADR-0012 — this node held an
		// m_bound map before, which made one definition unable to serve two differently-bound
		// evaluations.)
		void compute(NodeEvaluation&) const override {}
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

		// The reading twin of GroupInputNode::addReserved (see there for why a reserved pin is static):
		// a pin the ENGINE reads back out of the interior — ADR-0021's `continue`, which is how a loop
		// body says whether to run another iteration.
		//
		// It takes a DEFAULT because of the trap GateNode::enable hit on 2026-08-15: "nobody wired it"
		// and "the thing wired to it was suppressed" are the same empty slot with opposite meanings.
		// A default seeds an input with NO incoming edge and never fills a connected one that produced
		// nothing (Node::addInput), so an unwired condition is transparent while a broken one
		// suppresses. Only a member of this node can reach that protected declaration, which is the
		// other half of why this seam exists.
		template <typename T>
		PortId addReserved(std::string name, Default<T> fallback)
		{
			return addInput<T>(std::move(name), std::move(fallback));
		}

		std::size_t boundaryCount() const { return inputCount(); }

		// Passthrough — the scheduler populated the inputs, and the host reads them from the
		// Evaluation (evaluation.value(output)). The delivered value is runtime, so it is not held
		// here either.
		void compute(NodeEvaluation&) const override {}
	};

	// A bindable graph input / readable graph output: IMMUTABLE RECIPE METADATA describing one pin —
	// where it is, what it is called, and what type it carries. A host enumerates these from the
	// definition (Graph::boundaryInputs / boundaryOutputs, both const) and then binds and reads
	// through an Evaluation: `evaluation.bind(input, value)`, run, `evaluation.value(output)`.
	//
	// They deliberately carry no way to read or write a value. A bound value and a delivered value
	// are RUNTIME — one definition may be driven by several evaluations at once, each bound
	// differently — so a handle that could set one would be a handle to the wrong thing (ADR-0012).
	struct BoundaryPin
	{
		PortAddress port;		   // which pin, on which boundary node
		std::string name;		   // the pin's name: the cli flag, the Interface label, the on-disk edge key
		std::type_index type;	   // declared payload type — what a binder or codec dispatches on
		std::string_view typeName; // that type, human-readable (meta::typeName)
	};

	using BoundaryInput = BoundaryPin;
	using BoundaryOutput = BoundaryPin;
} // namespace lain::flow
