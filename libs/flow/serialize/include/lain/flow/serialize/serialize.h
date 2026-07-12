#pragma once

#include "lain/flow/serialize/loadresult.h"
#include "lain/flow/serialize/valuecodecs.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>

#include <cstdint>

// Graph <-> data::Value — the graph walk (WORK.md Tier A #1). Serializes a graph's RECIPE (node
// kinds + params + name-addressed edges), never computed port values: the graph re-runs to
// reproduce them. Node kinds come from the Factory (keyOf); param values from the ValueCodecs
// registry. The Value then rides a codec (lain::io::data) to bytes.
//
// Handles plain compute nodes, dynamic pins / boundary nodes, name-addressed edges, and the
// adapter-owned "editor" section (round-tripped opaquely).
namespace lain::flow::serialize
{
	// The document schema version at the root. Bumped only on an incompatible ENCODING change; a
	// document with a newer version fails to load (a format from the future can't be half-understood).
	inline constexpr std::int64_t kFormatVersion = 1;

	// Serialize a graph to a data::Value document { version, nodes, edges, editor }. A node whose type
	// is not registered in `factory` (no kind), a param whose type has no `codecs` entry, or a dynamic
	// pin whose type has no port-type key is skipped — a Value carries no issue list, so save is
	// silently best-effort; a clean graph serialises whole. `editor` is the adapter's per-node opaque
	// metadata (keyed by live NodeId), embedded under "editor" keyed by file id; pass {} for none.
	[[nodiscard]] data::Value toValue(const Graph& graph, const core::Factory<Node>& factory, const ValueCodecs& codecs, const EditorData& editor = {});

	// Rebuild a graph from a document, best-effort. Nodes are created via `factory` (by kind) with
	// FRESH ids (the file's ids are remapped — so this doubles as subgraph paste), params read via
	// `codecs`, edges reconnected by port name. Returns the Graph + issues; a too-new version yields
	// an empty graph + a fatal Error. Accepts a Value straight from toValue or one decoded from JSON
	// (a positive Int may arrive as UInt — both are read).
	[[nodiscard]] LoadResult fromValue(const data::Value& document, const core::Factory<Node>& factory, const ValueCodecs& codecs);
} // namespace lain::flow::serialize
