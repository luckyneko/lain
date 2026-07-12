#pragma once

#include <lain/flow/graph.h>

#include <string>
#include <vector>

namespace lain::flow::serialize
{
	// How serious a load problem is. A host thresholds on it: a gui may treat any issue as "did not
	// load cleanly"; a cli may refuse only on an Error.
	enum class Severity
	{
		Warning, // recoverable: an item was skipped, the rest loaded (an unknown param, a rejected edge)
		Error,	 // a structural loss (an unknown node kind dropped, a too-new document)
	};

	// One thing that went wrong during a load — both logged (lain::log) and returned here, so a host
	// can show it rather than only find it in a console.
	struct LoadIssue
	{
		Severity severity;
		std::string message;
	};

	// The outcome of loading a Graph: a best-effort Graph plus what went wrong. clean() == a full,
	// issue-free load. A partial load is still an INVARIANT-VALID Graph (it is rebuilt through
	// Graph's primitives), just possibly incomplete — the engine reports, the host decides policy.
	struct LoadResult
	{
		Graph graph;
		std::vector<LoadIssue> issues;

		bool clean() const { return issues.empty(); }
	};
} // namespace lain::flow::serialize
