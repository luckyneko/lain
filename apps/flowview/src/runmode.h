#pragma once

#include <lain/core/factory.h>
#include <lain/flow/node.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// The headless subcommands. Both build the example scene when `graphPath` is empty (so they run
	// with no file), else load it. Assume the caller has already registered the example nodes, the
	// scene serialization (port types + json), and the image codecs.

	// `run`: bind the graph's boundary inputs from `bindings` (--<name> <value> tokens), run it,
	// dump it to stdout, write bound outputs, and — if `savePath` is set — serialize the recipe.
	// An image boundary binds by loading the path; other types aren't cli-bindable yet. Returns 0.
	int runGraph(const std::string& graphPath, const std::string& savePath,
				 const std::vector<std::string>& bindings,
				 const lain::core::Factory<lain::flow::Node>& factory, std::uint32_t exampleSize);

	// `list`: print the graph's boundary inputs / outputs as `--<name> : <type>` (the flags `run`
	// accepts). Returns 0, or 1 if the graph couldn't be loaded.
	int listGraph(const std::string& graphPath, const lain::core::Factory<lain::flow::Node>& factory);
} // namespace flowview
