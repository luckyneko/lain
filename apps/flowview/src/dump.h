#pragma once

#include <iosfwd>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

namespace flowview
{
	// Writes a human-readable dump of every node and its ports (in dependency order) to
	// `out` — the cli-mode output. CPU port values print as text; a lain::image::Image
	// output reports its extent + corner pixels, read straight from the CPU buffer (which
	// proves the node actually ran). No device needed — the nodes are pure CPU.
	void dumpGraph(std::ostream& out, const lain::flow::Graph& graph, const lain::flow::Evaluation& evaluation);
} // namespace flowview
