#pragma once

// One rule, stated once: a node setting that may be either CONFIGURED or WIRED.
//
// A param is per-node configuration and serialises with the graph; an input port is a value the graph
// supplies while it runs. Several source nodes want both — a fixed default you set in the inspector,
// overridable by wiring something in — and the map made that necessary rather than merely nice: every
// element of a map shares one definition, so anything that must differ per element has to arrive as a
// value, not a param.
//
// The convention, matching a Select's connectable `selector`: the input is OPTIONAL, and a value on it
// WINS. Unconnected, the param stands, so an existing document behaves exactly as it did.
//
// Here rather than in flow core: this is a node-authoring convenience over two accessors core already
// has, and core does not need API for it.

#include "lain/flow/evaluation.h"
#include "lain/flow/node.h"

namespace lain::flow::example
{
	// The value for `input` if it carries one, else the value of `param`. `T` must be the declared type
	// of both — a mismatch is a programming error, and reads as the param (the input simply does not
	// hold a T).
	template <typename T>
	const T& setting(const Node& node, const NodeEvaluation& evaluation, PortId input, PortId param)
	{
		const PortValue& wired = evaluation.input(input);
		return wired.holds<T>() ? wired.get<T>() : node.param(param).get<T>();
	}
} // namespace lain::flow::example
