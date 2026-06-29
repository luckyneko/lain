#pragma once

#include <archimedes/acmForward.h>
#include <lain/flow/types.h>

#include <cstdint>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// Builds flowview's smoke scene into `graph`: flow-example's GradientNode, a GPU
	// source that emits a `size`x`size` acm::Texture on its output port. Shared by
	// both modes — cli-mode evaluates it and dumps the result, gui-mode previews it.
	// Returns the id of the texture-producing node.
	lain::flow::NodeId buildExampleScene(lain::flow::Graph& graph, acm::Device device, std::uint32_t size);
} // namespace flowview
