#pragma once

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/node.h>
#include <lain/flow/serialize/loadresult.h>
#include <lain/flow/serialize/serialize.h>
#include <lain/flow/serialize/valuecodecs.h>

#include <filesystem>
#include <string>

namespace lain::flow
{
	class Graph;
}

namespace flowview
{
	// flowview's serialization wiring: the value-serializer registry for its param types, the
	// one-time registrations, and the save/load facade over flow::serialize + lain::io::data. Kept
	// here (not in flow::serialize) because it names the concrete param/port types the app uses.

	// The value-serializer registry for flowview's param types: int / float (blur), path
	// (loadimage), image::ColorRGBf (tint). The app-populated ValueCodecs the graph walk needs.
	lain::flow::serialize::ValueCodecs sceneCodecs();

	// One-time registration serialization needs: the image::Image port type (so boundary pins
	// replay) and the data codecs (json). Idempotent — safe to call in either mode's startup.
	// Registers the scene's value conversions too, so both halves of "what types does this app
	// know" are established by one call.
	void registerSceneSerialization();

	// What a CAST node may convert, and how (ADR-0022) — the value-conversion registry's contents
	// for this app. Called by registerSceneSerialization; separate so a test can ask for the
	// conversions alone. Idempotent.
	void registerSceneConversions();

	// Save `graph` as JSON at `uri` (io::data picks the codec by extension). `editor` is the
	// adapter's per-node metadata (canvas positions), keyed by NodeId. Returns false on a write /
	// encode failure (logged).
	bool saveGraph(const std::string& uri, const lain::flow::Graph& graph,
				   const lain::core::Factory<lain::flow::Node>& factory,
				   const lain::flow::serialize::EditorTree& editor = {});

	// The canonical key one template file is known by: the cycle guard compares it, and the
	// TemplateCache is keyed on it. ONE function, because a key that is computed two ways is a key
	// that eventually disagrees with itself — and a disagreement here is silent (an invalidation that
	// misses simply keeps serving the old definition).
	std::string templateKey(const std::filesystem::path& path);

	// The template resolver a load uses for LINKED groups: `source` is read relative to
	// `documentDir` (so a project folder stays portable) and canonicalised through templateKey.
	// Exposed for callers that load a document themselves.
	lain::flow::serialize::TemplateResolver templateResolver(const std::filesystem::path& documentDir);

	// Load a graph from JSON at `uri`. Best-effort: an unreadable file is a fatal Error in the
	// returned LoadResult (which then carries an empty graph).
	//
	// `cache` is the host's template cache — what makes several linked groups on one file share a
	// single definition (ADR-0013). Required rather than defaulted: omitting it is silent, and what it
	// silently costs is the whole point of having one. Pass nullptr where there is genuinely no host
	// to own a cache, and mean it.
	lain::flow::serialize::LoadResult loadGraph(const std::string& uri,
												const lain::core::Factory<lain::flow::Node>& factory,
												lain::flow::serialize::TemplateCache* cache);

	// Serialize `graph` (+ its canvas `editor` layout) to a data::Value — the same document Save
	// writes, but kept in RAM. The undo history is a stack of these (Save-to-RAM); a restore feeds
	// one back through restoreGraph (Load-from-RAM). Uses sceneCodecs(), so it captures exactly what
	// the on-disk format does — structure, params, node/pin names, dynamic pins, layout — and no
	// runtime-only state (bound boundary values), which is why two snapshots differing only in a bind
	// compare equal.
	lain::data::Value snapshotGraph(const lain::flow::Graph& graph,
									const lain::core::Factory<lain::flow::Node>& factory,
									const lain::flow::serialize::EditorTree& editor = {});

	// Rebuild a graph from a snapshot (or any document Value) — fromValue with sceneCodecs(). A
	// snapshot came from snapshotGraph, so the result is normally clean; any issues ride in the
	// LoadResult as usual.
	//
	// `documentDir` is the folder the snapshot's document lives in, and LINKED GROUPS need it: their
	// `source` is stored relative to that document, so without it every linked group restores as an
	// unresolved placeholder — its interior empties and it stops producing output. Required rather
	// than defaulted, because that is exactly the bug an easy default invited (an undo silently
	// emptied every linked group). Pass the parent of the open document's path; empty is legitimate
	// for an untitled document, where a relative source simply resolves against the working
	// directory, and an absolute one is unaffected either way.
	//
	// `cache` is the host's template cache, as for loadGraph. A restore deliberately does NOT reset it:
	// an undo is not a document change, and keeping it is what holds a template's inner node ids — and
	// therefore preview keys and canvas ints — steady across one.
	lain::flow::serialize::LoadResult restoreGraph(const lain::data::Value& document,
												   const lain::core::Factory<lain::flow::Node>& factory,
												   const std::filesystem::path& documentDir,
												   lain::flow::serialize::TemplateCache* cache);
} // namespace flowview
