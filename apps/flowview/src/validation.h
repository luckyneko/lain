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

	// Every node, at ANY level, whose last compute threw — one Error row each, with what it threw
	// (ADR-0025: a run records a failure against the node, in the evaluation it threw in). Read from the
	// published evaluation, whose own definition names the nodes; its ids are the document's.
	//
	// Every level, not just the active one, because a failure inside a group is exactly what a user
	// has not descended to see. A row on the ACTIVE level locates its node; one on a level below leads
	// there. The elements of a map share one definition, so the same node failing in several of them
	// is ONE row naming the first and counting the rest — a broken folder must not bury the panel,
	// which is the map-hole row's rule too.
	std::vector<Issue> collectFailures(const lain::flow::Evaluation& published, const GraphPath& activePath);
} // namespace flowview
