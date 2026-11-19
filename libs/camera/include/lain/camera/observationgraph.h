#pragma once

#include <cstdint>
#include <vector>

// The camera observation graph (CONTEXT.md): which of a method's views share evidence, inferred from
// that evidence and never declared. A vertex is a method's view: a camera in registration, a frame in
// targetless calibration. It is lain::camera's, not a method module's, because both methods build one
// and neither may depend on the other (ADR-0016), so this header includes nothing from registration/
// or calibration/. Registration names these types where its callers look for them
// (registration/observationgraph.h), beside the weak-bridge judgement that is registration's alone.
namespace lain::camera
{
	// An edge of the graph: two views that share evidence units (capture groups in which both saw the
	// board, or tracks both observed). Views are numbered; a report names them.
	struct GraphEdge
	{
		std::uint32_t a = 0; // a < b
		std::uint32_t b = 0;
		std::uint32_t shared = 0; // units the two views share
		bool bridge = false;	  // removing it would split its component
	};

	// The graph, its vertices named as registration reports them: `cameras` is how many views there
	// are, and `sharedPerCamera` how many units each shares with another.
	struct ObservationGraph
	{
		std::uint32_t cameras = 0;
		std::vector<GraphEdge> edges;						// ascending by (a, b)
		std::vector<std::vector<std::uint32_t>> components; // each ascending, ordered by its first view
		std::vector<std::uint32_t> sharedPerCamera;			// units the view shares with another
	};

	// The graph over `views` views where `units[u]` lists the views that saw evidence unit u. A unit
	// seen by one view connects nothing and is ignored. A view in no unit is a component of its own.
	ObservationGraph observationGraph(std::uint32_t views, const std::vector<std::vector<std::uint32_t>>& units);
} // namespace lain::camera
