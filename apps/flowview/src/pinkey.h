#pragma once

#include <lain/flow/types.h> // NodeId, PortId

namespace flowview
{
	// Identifies one port's preview by its stable logical position (node + direction + port) — a key
	// that survives recomputes but not node deletion. No GPU/ImGui types. Shared across panes
	// (Inspector / Interface / Preview) and the preview cache, so it lives on its own rather than
	// inside any one of them.
	//
	// It carries no LEVEL: nothing here says which graph of a nested document the pin belongs to.
	// That is why the host clears the preview cache on navigation — see MainWindow. (M6 step 5 gives
	// it an EvalPath, at which point the clear becomes a memory choice rather than a correctness one.)
	struct PinKey
	{
		lain::flow::NodeId node;
		bool output;
		lain::flow::PortId port; // stable id, so a preview survives sibling pins changing

		bool operator==(const PinKey& o) const { return node == o.node && output == o.output && port == o.port; }
		bool operator!=(const PinKey& o) const { return !(*this == o); }

		bool operator<(const PinKey& o) const
		{
			if (node != o.node)
				return node < o.node;
			if (output != o.output)
				return output < o.output;
			return port < o.port;
		}
	};
} // namespace flowview
