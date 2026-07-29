#pragma once

#include "lain/flow/serialize/loadresult.h"
#include "lain/flow/serialize/valuecodecs.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// Graph <-> data::Value — the graph walk (WORK.md Tier A #1). Serializes a graph's RECIPE (node
// kinds + params + name-addressed edges), never computed port values: the graph re-runs to
// reproduce them. Node kinds come from the Factory (keyOf); param values from the ValueCodecs
// registry. The Value then rides a codec (lain::io::data) to bytes.
//
// Handles plain compute nodes, dynamic pins / boundary nodes, name-addressed edges, group nodes
// (recursively — see below), and the adapter-owned "editor" section (round-tripped opaquely).
//
// The format has two shapes: a GRAPH BODY is { nodes, edges, editor }, and a DOCUMENT is a body
// plus { version }. The root and every template file are documents; an INLINE group embeds a body
// under its node's "graph" key, so nesting is the same shape all the way down. A LINKED group
// instead stores "source" (its template's path) plus "interface" (the cached pin names + types).
namespace lain::flow::serialize
{
	// A template resolved to the document it names, plus a CANONICAL key identifying it (an absolute
	// path, typically). The key is what the recursion guard compares, so two different spellings of
	// one file must produce the same key or a cycle could slip through.
	struct ResolvedTemplate
	{
		std::string key;
		data::Value document;
	};

	// How a linked group's `source` becomes a document. INJECTED, because flow core does no file
	// I/O — the app supplies a closure that resolves the path (relative to the parent document) and
	// reads it. A default-constructed resolver resolves nothing, which is a legitimate mode, not an
	// error: every linked group then loads UNRESOLVED from its interface cache, which is what a
	// headless tool listing a graph's interface wants.
	using TemplateResolver = std::function<std::optional<ResolvedTemplate>(const std::string& source)>;

	// The document schema version at the root. Bumped only on an incompatible ENCODING change; a
	// document with a newer version fails to load (a format from the future can't be half-understood).
	inline constexpr std::int64_t kFormatVersion = 1;

	// Serialize a graph to a data::Value document { version, nodes, edges, editor }. A node whose type
	// is not registered in `factory` (no kind), a param whose type has no `codecs` entry, or a dynamic
	// pin whose type has no port-type key is skipped — a Value carries no issue list, so save is
	// silently best-effort; a clean graph serialises whole. `editor` is the adapter's per-node opaque
	// metadata (keyed by live NodeId), embedded under "editor" keyed by file id; pass {} for none.
	[[nodiscard]] data::Value toValue(const Graph& graph, const core::Factory<Node>& factory, const ValueCodecs& codecs, const EditorTree& editor = {});

	// Rebuild a graph from a document, best-effort. Nodes are created via `factory` (by kind) with
	// FRESH ids (the file's ids are remapped — so this doubles as subgraph paste), params read via
	// `codecs`, edges reconnected by port name. Returns the Graph + issues; a too-new version yields
	// an empty graph + a fatal Error. Accepts a Value straight from toValue or one decoded from JSON
	// (a positive Int may arrive as UInt — both are read).
	//
	// A group node's inner graph is rebuilt recursively — from the embedded body (inline) or from the
	// document `resolver` returns (linked) — and its own ports are then re-derived by
	// edit::syncGroupPorts, so they are never stored. A linked group is RECTIFIED against its cached
	// interface: a pin the template no longer has, or one whose type changed, is reported as an issue
	// rather than quietly taking the parent's edges with it. A template that (transitively) links
	// itself is refused by canonical key.
	[[nodiscard]] LoadResult fromValue(const data::Value& document, const core::Factory<Node>& factory, const ValueCodecs& codecs,
									   const TemplateResolver& resolver = {});
} // namespace lain::flow::serialize
