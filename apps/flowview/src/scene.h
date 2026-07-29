#pragma once

#include <lain/core/factory.h>
#include <lain/flow/node.h>
#include <lain/flow/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// A category grouping of node kinds (factory keys) for the Add menu, in display order. App-side
	// presentation metadata — flow has no category concept; this mirrors the canvasstyle colour grouping
	// (a future "node catalog" could unify category + colour + display name per kind in one place).
	struct NodeCategory
	{
		std::string name;
		std::vector<std::string> keys;
	};
	const std::vector<NodeCategory>& nodeCatalog();

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
