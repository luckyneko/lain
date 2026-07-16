#pragma once

#include <lain/flow/types.h> // PortId

#include <cstdint>

namespace flowview
{
	// Identifies one port's preview by its stable logical position (node + direction +
	// index) — a key that survives recomputes but not node deletion. No GPU/ImGui types.
	// Shared across panes (Inspector / Interface / Preview) and the preview cache, so it
	// lives on its own rather than inside any one of them.
	struct PinKey
	{
		std::uint64_t node;
		bool output;
		lain::flow::PortId port; // stable id, so a preview survives sibling pins changing

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
