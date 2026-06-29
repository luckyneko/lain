#pragma once

#include <archimedes/acmForward.h>

#include <iosfwd>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// Writes a human-readable dump of every node and its ports (in dependency order)
	// to `out` — the cli-mode output. CPU port values print as text; an output port
	// carrying an acm::Texture is read back through `device` and reported by extent +
	// corner pixels, which proves the GPU node actually ran. `device` may be invalid
	// (no driver) — texture ports then report just their declared type.
	void dumpGraph(std::ostream& out, const lain::flow::Graph& graph, acm::Device device);
} // namespace flowview
