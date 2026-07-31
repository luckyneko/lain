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
	void registerSceneSerialization();

	// Save `graph` as JSON at `uri` (io::data picks the codec by extension). `editor` is the
	// adapter's per-node metadata (canvas positions), keyed by NodeId. Returns false on a write /
	// encode failure (logged).
	bool saveGraph(const std::string& uri, const lain::flow::Graph& graph,
				   const lain::core::Factory<lain::flow::Node>& factory,
				   const lain::flow::serialize::EditorTree& editor = {});

	// The template resolver a load uses for LINKED groups: `source` is read relative to
	// `documentDir` (so a project folder stays portable) and canonicalised, since that canonical path
	// is the key the recursion guard compares. Exposed for callers that load a document themselves.
	lain::flow::serialize::TemplateResolver templateResolver(const std::filesystem::path& documentDir);

	// Load a graph from JSON at `uri`. Best-effort: an unreadable file is a fatal Error in the
	// returned LoadResult (which then carries an empty graph).
	lain::flow::serialize::LoadResult loadGraph(const std::string& uri,
												const lain::core::Factory<lain::flow::Node>& factory);

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
	lain::flow::serialize::LoadResult restoreGraph(const lain::data::Value& document,
												   const lain::core::Factory<lain::flow::Node>& factory);
} // namespace flowview
