#pragma once

#include <cstdint>
#include <vector>

namespace lain::camera::registration
{
	// An edge of the camera observation graph: two cameras that share evidence units (capture groups
	// in which both saw the board, or tracks both observed). Cameras are numbered; a report names them.
	struct GraphEdge
	{
		std::uint32_t a = 0; // a < b
		std::uint32_t b = 0;
		std::uint32_t shared = 0; // units the two cameras share
		bool bridge = false;	  // removing it would split its component
	};

	// The camera observation graph (CONTEXT.md): inferred from the evidence, never declared.
	struct ObservationGraph
	{
		std::uint32_t cameras = 0;
		std::vector<GraphEdge> edges;						// ascending by (a, b)
		std::vector<std::vector<std::uint32_t>> components; // each ascending, ordered by its first camera
		std::vector<std::uint32_t> sharedPerCamera;			// units the camera shares with another
	};

	// The graph over `cameras` cameras where `units[u]` lists the cameras that saw evidence unit u. A
	// unit seen by one camera connects nothing and is ignored. A camera in no unit is a component of
	// its own.
	ObservationGraph observationGraph(std::uint32_t cameras, const std::vector<std::vector<std::uint32_t>>& units);

	// The bridges resting on fewer than `minimumShared` units (CONTEXT.md, "Weak bridge").
	std::vector<GraphEdge> weakBridges(const ObservationGraph& graph, std::uint32_t minimumShared);
} // namespace lain::camera::registration
