#pragma once

#include "lain/camera/observationgraph.h"

#include <cstdint>
#include <vector>

namespace lain::camera::registration
{
	// The camera observation graph is lain::camera's (observationgraph.h), since targetless
	// calibration builds one too; registration names it where its callers look for it.
	using camera::GraphEdge;
	using camera::ObservationGraph;
	using camera::observationGraph;

	// The bridges resting on fewer than `minimumShared` units (CONTEXT.md, "Weak bridge"): a fitness
	// judgement, and only registration makes one.
	std::vector<GraphEdge> weakBridges(const ObservationGraph& graph, std::uint32_t minimumShared);
} // namespace lain::camera::registration
