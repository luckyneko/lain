#pragma once

#include <lain/data/value.h>
#include <lain/flow/graph.h>

#include <map>
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

	// Per-node opaque editor metadata (canvas position, colour, collapsed-state, ...) owned by the
	// ADAPTER and round-tripped by flow::serialize without interpretation. Keyed by NodeId: the
	// adapter passes its live-id map to toValue, and fromValue hands it back re-keyed (through the
	// file-id remap) to the FRESH loaded ids — so the adapter applies it directly, never seeing a
	// file id.
	using EditorData = std::map<NodeId, data::Value>;

	// The outcome of loading a Graph: a best-effort Graph plus what went wrong + the re-keyed editor
	// metadata. clean() == a full, issue-free load. A partial load is still an INVARIANT-VALID Graph
	// (it is rebuilt through Graph's primitives), just possibly incomplete — the engine reports, the
	// host decides policy.
	struct LoadResult
	{
		Graph graph;
		std::vector<LoadIssue> issues;
		EditorData editor; // adapter metadata, re-keyed to this graph's node ids

		bool clean() const { return issues.empty(); }
	};
} // namespace lain::flow::serialize
