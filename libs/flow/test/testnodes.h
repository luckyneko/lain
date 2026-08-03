#pragma once

// Shared GPU-free node fixtures for the flow unit tests: a constant int source, a
// two-input adder, and a float sink (for provoking type mismatches). Kept in a named
// namespace so both the graph and edit suites can share them without a copy.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/node.h"

#include <cstddef>

namespace lain::flow::test
{
	// A constant source: one int output, set on compute.
	struct ConstInt : Node
	{
		int value;
		PortId out;
		explicit ConstInt(int v)
			: Node("ConstInt")
			, value(v)
		{
			out = addOutput<int>("value");
		}
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).set(value); }
	};

	// Two int inputs -> their sum.
	struct AddInt : Node
	{
		PortId a, b, sum;
		AddInt()
			: Node("Add")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			sum = addOutput<int>("sum");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(sum).set(evaluation.input(a).get<int>() + evaluation.input(b).get<int>());
		}
	};

	// One float input — used to provoke a type mismatch against an int output.
	struct SinkFloat : Node
	{
		PortId in;
		SinkFloat()
			: Node("Sink")
		{
			in = addInput<float>("x");
		}
		void compute(NodeEvaluation&) const override {}
	};

	// Positional value lookup, for tests that build a fixed graph and know its ports by declaration
	// order — which is exactly what a position is for. Production code addresses a port by the PortId
	// its declaration returned; a test writing `output(g, e, n, 0)` is saying "the first output",
	// which is what it means.
	inline const PortValue& output(const Graph& graph, const Evaluation& evaluation, NodeId node, std::size_t index)
	{
		return evaluation.value(PortAddress{node, graph.node(node).output(index).id()});
	}

	inline const PortValue& input(const Graph& graph, const Evaluation& evaluation, NodeId node, std::size_t index)
	{
		return evaluation.value(PortAddress{node, graph.node(node).input(index).id()});
	}
} // namespace lain::flow::test
