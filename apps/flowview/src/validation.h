#pragma once

// Live validation of the graph on screen: the Issues pane's content, apart from the pane.
//
// A pure function of a DEFINITION plus its runtime state, and split out for that reason — it needs
// no window, no device and no ImGui, so the rules it encodes are testable directly (see
// test/test_validation.cpp). They were not, while they sat inside the pane, which is how the pane
// spent two milestones telling every Blur, Gate, Select and Loop that a defaulted input it seeds
// itself was a missing connection.

#include "appcontext.h" // Issue — the row type these produce
#include "groupnav.h"	// GraphPath — a map element's row names the level to descend into

#include <vector>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

namespace flowview
{
	// Everything wrong with `graph` right now, recomputed from scratch: a required input with no
	// incoming edge, an unused output, an unconvertible Cast, a map whose gather found a hole.
	// Recomputed each frame by the pane — cheap at prototyping scale, and self-clearing as the graph
	// is fixed, which is why nothing about it is stored.
	//
	// `activePath` is the level `graph` sits at, needed only to build the absolute path a map
	// element's row navigates into.
	std::vector<Issue> collectIssues(const lain::flow::Graph& graph, const lain::flow::Evaluation& evaluation,
									 const GraphPath& activePath);
} // namespace flowview
