#pragma once

#include <cstdint>
#include <vector>

namespace lain::camera::registration
{
	// An edge of the camera observation graph: two cameras that saw the board in the same capture
	// groups. Cameras are numbered; a report names them.
	struct GraphEdge
	{
		std::uint32_t a = 0; // a < b
		std::uint32_t b = 0;
		std::uint32_t sharedGroups = 0;
		bool bridge = false; // removing it would split its component
	};

	// The camera observation graph (CONTEXT.md): inferred from the evidence, never declared.
	struct ObservationGraph
	{
		std::uint32_t cameras = 0;
		std::vector<GraphEdge> edges;						// ascending by (a, b)
		std::vector<std::vector<std::uint32_t>> components; // each ascending, ordered by its first camera
		std::vector<std::uint32_t> groupsPerCamera;			// groups in which the camera saw the board with another
	};

	// The graph over `cameras` cameras where `groups[g]` lists the cameras that saw the board in
	// capture group g. A group seen by one camera connects nothing and is ignored. A camera in no
	// group is a component of its own.
	ObservationGraph observationGraph(std::uint32_t cameras, const std::vector<std::vector<std::uint32_t>>& groups);

	// The bridges resting on fewer than `minimumSharedGroups` groups (CONTEXT.md, "Weak bridge").
	std::vector<GraphEdge> weakBridges(const ObservationGraph& graph, std::uint32_t minimumSharedGroups);
} // namespace lain::camera::registration
