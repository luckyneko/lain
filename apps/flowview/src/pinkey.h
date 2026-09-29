#pragma once

#include <lain/flow/types.h> // EvalPath (which level the pin is on), PortAddress

namespace flowview
{
	// Identifies one port's preview: WHICH EVALUATION, WHICH NODE, WHICH PORT. Three real axes, no
	// GPU/ImGui types. Shared across panes (Inspector / Interface / Preview) and the preview cache,
	// so it lives on its own rather than inside any one of them.
	//
	// The level used to be missing, and the host worked around it by clearing the preview cache on
	// every navigation — because node ids repeated across levels, so an inner pin could find an
	// outer pin's entry and the panes would show another graph's image as if it were this one's
	// (M5, bug nine). Node ids stopped repeating with ADR-0011, but the key was still saying less
	// than it meant: two DIFFERENT evaluations of one definition (the target workload — N streams
	// through one subgraph) have the same node and port ids by design. So the level is part of the
	// key, and correctness no longer rests on remembering to clear.
	//
	// `path` is the evaluation coordinate ADR-0012 calls an EvalPath — flow's own type, the one a run
	// reports where each node computed by (M14 slice 7), so a pin key and a run's report name a level
	// the same way. It walks the graph too: every element of a map shares one definition, so the graph
	// walk simply ignores the index.
	//
	// There is no direction field: a PortId is minted per NODE across both sides (M6 step 2), so a
	// PortAddress already names one port unambiguously.
	struct PinKey
	{
		lain::flow::EvalPath path; // empty = the root graph
		lain::flow::PortAddress port;

		bool operator==(const PinKey& o) const { return path == o.path && port == o.port; }
		bool operator!=(const PinKey& o) const { return !(*this == o); }

		// Ordered so a PinKey can key a std::map / std::set. Level first, so one level's entries sit
		// together — which is what makes "drop everything for the level being left" a range, not a scan.
		bool operator<(const PinKey& o) const
		{
			if (path != o.path)
				return path < o.path;
			if (port.node != o.port.node)
				return port.node < o.port.node;
			return port.port < o.port.port;
		}
	};
} // namespace flowview
