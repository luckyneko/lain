#pragma once

// Shared GPU-free node fixtures for the flow unit tests: a constant int source, a
// two-input adder, and a float sink (for provoking type mismatches). Kept in a named
// namespace so both the graph and edit suites can share them without a copy.

#include <lain/flow/node.h>

namespace lain::flow::test
{
	// A constant source: one int output, set on compute.
	struct ConstInt : Node
	{
		int value;
		PortIndex out;
		explicit ConstInt(int v)
			: Node("ConstInt")
			, value(v)
		{
			out = addOutput<int>("value");
		}
		void compute() override { output(out).set(value); }
	};

	// Two int inputs -> their sum.
	struct AddInt : Node
	{
		PortIndex a, b, sum;
		AddInt()
			: Node("Add")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			sum = addOutput<int>("sum");
		}
		void compute() override { output(sum).set(input(a).get<int>() + input(b).get<int>()); }
	};

	// One float input — used to provoke a type mismatch against an int output.
	struct SinkFloat : Node
	{
		PortIndex in;
		SinkFloat()
			: Node("Sink")
		{
			in = addInput<float>("x");
		}
		void compute() override {}
	};
} // namespace lain::flow::test
