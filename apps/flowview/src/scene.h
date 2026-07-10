#pragma once

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
	// Register flow-example's node types into `factory` (gradient / tint / blur /
	// loadimage), with construction values captured in each creator's closure. This is
	// the palette the editor draws from. Call once the device is live.
	void registerExampleNodes(lain::core::Factory<lain::flow::Node>& factory, std::uint32_t size);

	// Build flowview's smoke scene into `graph` on the M4 boundary model: a host-bound
	// GroupInputNode ("source") -> tint -> blur -> GroupOutputNode ("result"). The graph
	// declares its interface (one image in, one image out); the host binds it — cli from
	// --input/--output, gui from the Interface panel. tint/blur come from `factory`
	// (populated by registerExampleNodes). Shared by both modes.
	void buildExampleScene(lain::flow::Graph& graph, const lain::core::Factory<lain::flow::Node>& factory);

	// Bind the graph's first boundary input to a generated `size`x`size` gradient image, so a
	// fresh gui-mode / bare `--headless` shows a result on launch; the Interface panel (gui) or
	// --input (cli) rebinds it to a real file.
	void bindDefaultInput(lain::flow::Graph& graph, std::uint32_t size);
} // namespace flowview
