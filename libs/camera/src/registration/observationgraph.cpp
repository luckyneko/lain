#include "lain/camera/registration/observationgraph.h"

namespace lain::camera::registration
{
	std::vector<GraphEdge> weakBridges(const ObservationGraph& graph, std::uint32_t minimumShared)
	{
		std::vector<GraphEdge> out;
		for (const GraphEdge& e : graph.edges)
		{
			if (e.bridge && e.shared < minimumShared)
				out.push_back(e);
		}
		return out;
	}
} // namespace lain::camera::registration
