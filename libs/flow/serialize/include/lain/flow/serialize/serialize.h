#pragma once

#include "lain/flow/serialize/loadresult.h"
#include "lain/flow/serialize/templatecache.h"
#include "lain/flow/serialize/valuecodecs.h"

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h> // LinkedGroupNode — resolveLinkedGroup's subject
#include <lain/flow/node.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

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
//
// A MAP embeds a body and, uniquely, its OWN ports under "interface" — its mirroring is
// under-determined by one bit per input pin, so they cannot be re-derived. A LOOP embeds a body and
// a "loop" section: { index, continue, carries: [{ in, out }] } — the carry PAIRING, which no
// declaration can encode (a carried and an invariant Image are the same type), plus the two reserved
// pin names, which are stored because a reserved pin is renameable and an edge into one is
// name-addressed like any other. A loop's own ports are NOT stored: its face derives exactly as a
// plain group's.
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
	// An OLDER one is migrated: fromValue routes a whole document by this field, a private migrator
	// rewrites its data::Value into the current shape, and the sole decoder then loads that — so
	// dropping an old version later deletes a translation unit, not a second loader.
	//
	//   1 — node ids were per-body integers, renumbered 1..N on every save.
	//   2 — node ids are uuid strings, preserved across saves (ADR-0011).
	inline constexpr std::int64_t kFormatVersion = 2;

	// Serialize a graph to a data::Value document { version, nodes, edges, editor }. A node whose type
	// is not registered in `factory` (no kind), a param whose type has no `codecs` entry, or a dynamic
	// pin whose type has no port-type key is skipped — a Value carries no issue list, so save is
	// silently best-effort; a clean graph serialises whole. `editor` is the adapter's per-node opaque
	// metadata (keyed by live NodeId), embedded under "editor" keyed by file id; pass {} for none.
	[[nodiscard]] data::Value toValue(const Graph& graph, const core::Factory<Node>& factory, const ValueCodecs& codecs, const EditorTree& editor = {});

	// Rebuild a graph from a document, best-effort. Nodes are created via `factory` (by kind) and
	// RESTORED AS THEMSELVES — a load preserves the ids the file recorded, which is what lets undo,
	// the host's active path and a canvas selection survive a rebuild. Params are read via `codecs`,
	// edges reconnected by port name. Returns the Graph + issues; a known older version is migrated
	// (see kFormatVersion), while a missing, unknown or too-new one yields an empty graph + a fatal
	// Error rather than a guess. Accepts a Value straight from toValue or one decoded from JSON.
	//
	// A node whose id is missing, nil or unreadable is given a fresh one and reported; so is the
	// second node to claim an id already taken — identity is never overwritten. A document lacking
	// either boundary node gets that one minted, since the pair is a Graph invariant.
	//
	// A group node's inner graph is rebuilt recursively — from the embedded body (inline) or from the
	// document `resolver` returns (linked) — and its own ports are then re-derived by
	// edit::syncGroupPorts, so they are never stored. A linked group is RECTIFIED against its cached
	// interface: a pin the template no longer has, or one whose type changed, is reported as an issue
	// rather than quietly taking the parent's edges with it. A template that (transitively) links
	// itself is refused by canonical key.
	//
	// A template is a document in its own right, so it enters the version router independently of the
	// parent's version — a v1 template linked from a v2 parent still opens.
	//
	// `cache` is the host's TemplateCache, and is what makes several linked groups built from one file
	// SHARE a single definition rather than each owning a copy (ADR-0013). It is consulted by canonical
	// key before a template is built, and a freshly built one is stored — unless it holds an unresolved
	// link of its own, which is a repairable state that must not be frozen. Passing none is a
	// legitimate mode (a one-shot headless load, a test): every instance then builds its own equal
	// copy, which is safe because node ids need only be unique WITHIN a graph and each instance's
	// values live in its own child Evaluation.
	[[nodiscard]] LoadResult fromValue(const data::Value& document, const core::Factory<Node>& factory, const ValueCodecs& codecs,
									   const TemplateResolver& resolver = {}, TemplateCache* cache = nullptr);

	// What resolving ONE linked group produced: the template's own layout (for that group's subtree of
	// the host's EditorTree) and whatever went wrong.
	struct ResolveResult
	{
		EditorTree editor;
		std::vector<LoadIssue> issues;

		bool clean() const
		{
			for (const LoadIssue& issue : issues)
			{
				if (issue.severity == Severity::Error)
					return false;
			}
			return true;
		}
	};

	// Resolve a single linked group whose `source` (and, optionally, cached interface) is already set —
	// the interactive "add a linked group pointing at this file" gesture. Identical in every respect to
	// what a document load does for each link it reads, because it IS that routine: same resolver, same
	// cycle guard, same cache participation, same rectification, same layout.
	//
	// It exists so a host does not hand-roll a second resolve path. That shape is what left a freshly
	// added linked group in default columns while the load path carried the template's layout, and it
	// would now also leave the added instance holding a private copy of a definition every other
	// instance shares. The caller still re-derives the group's outer ports (edit::syncGroupPorts),
	// exactly as it does after a load, since that needs the PARENT graph.
	[[nodiscard]] ResolveResult resolveLinkedGroup(LinkedGroupNode& linked, const core::Factory<Node>& factory,
												   const ValueCodecs& codecs, const TemplateResolver& resolver,
												   TemplateCache* cache);
} // namespace lain::flow::serialize
