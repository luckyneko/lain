#pragma once

#include <archimedes/acmForward.h>
#include <lain/core/factory.h>
#include <lain/flow/node.h>
#include <lain/flow/types.h>

#include <cstdint>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// Register flow-example's node types into `factory` (currently just the
	// GradientNode, keyed "gradient"), with the device + extent captured in each
	// creator's closure. This is the palette the editor will draw from; for now it
	// also backs the smoke scene below. Call once the device is live.
	void registerExampleNodes(lain::core::Factory<lain::flow::Node>& factory, acm::Device device, std::uint32_t size);

	// Build flowview's smoke scene into `graph`: create the GradientNode from
	// `factory` (which must have been populated by registerExampleNodes) and adopt it.
	// A GPU source emitting a `size`x`size` acm::Texture. Shared by both modes —
	// cli-mode evaluates it and dumps the result, gui-mode previews it. Returns the id
	// of the texture-producing node.
	lain::flow::NodeId buildExampleScene(lain::flow::Graph& graph, const lain::core::Factory<lain::flow::Node>& factory);
} // namespace flowview
