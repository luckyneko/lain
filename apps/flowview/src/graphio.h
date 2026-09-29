#pragma once

#include "runpolicy.h" // RunTrigger — a document's run trigger is one of its options

#include <lain/core/factory.h>
#include <lain/core/uri.h>
#include <lain/data/archive.h> // LAIN_SERIALIZE (DocumentOptions)
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

	// What flowview stores about a DOCUMENT as a whole, rather than about its nodes — kept in the
	// root's editor blob (EditorTree::graph), so flow never interprets it. Travels with the file,
	// because each of these is a choice about THAT graph (how expensive it is to run).
	//
	// Deliberately NOT part of an undo snapshot: switching a slow graph to Manual and then undoing the
	// param edits made before the switch must not flip it back to Live and start a run. So it is saved
	// by saveGraph, stripped from a load by takeDocumentOptions, and never seen by snapshotGraph.
	struct DocumentOptions
	{
		RunTrigger trigger = RunTrigger::Live;

		bool operator==(const DocumentOptions& other) const { return trigger == other.trigger; }
	};
	LAIN_SERIALIZE(DocumentOptions, trigger)

	// Save `graph` as JSON at `uri` (io::data picks the codec by extension). `editor` is the
	// adapter's layout (canvas positions, per level); `options` becomes the root's editor blob. The
	// blob is written only when `options` differs from the defaults, so a document that never changed
	// them keeps the bytes it had before options existed — and `options` OWNS the root blob: whatever
	// `editor.graph` held is replaced. Returns false on a write / encode failure (logged).
	bool saveGraph(const lain::core::Uri& uri, const lain::flow::Graph& graph,
				   const lain::core::Factory<lain::flow::Node>& factory,
				   const lain::flow::serialize::EditorTree& editor = {}, const DocumentOptions& options = {});

	// Read a loaded document's options out of its root editor blob, and STRIP the blob from
	// `result.editor`, so the host holds exactly ONE copy of them. The layout that remains becomes the
	// host's layout tree, the undo baseline and every snapshot after it: left in, the blob would ride
	// through the whole undo history as a second copy of the trigger — frozen at the value the file
	// was opened with, and one line away from being reapplied by a restore, which is exactly the undo
	// DocumentOptions refuses. A blob that is not options-shaped becomes the defaults plus a Warning
	// in `result.issues`; a single field this build cannot read (a trigger a newer flowview wrote)
	// falls back to its default, as lain::data reads every struct member; either way the document
	// itself still loads. No blob is the defaults, silently.
	DocumentOptions takeDocumentOptions(lain::flow::serialize::LoadResult& result);

	// The canonical uri one template file is known by: the cycle guard compares it, and the
	// TemplateCache is keyed on it. ONE function, because a key that is computed two ways is a key
	// that eventually disagrees with itself — and a disagreement here is silent (an invalidation that
	// misses simply keeps serving the old definition).
	//
	// It answers a core::Uri, and flow::serialize takes its .toString(): a cache key there is an
	// OPAQUE identity token, and flow must not interpret a source path (ADR-0013). That .toString()
	// at the call site is the seam where a name stops being one, which is worth seeing.
	lain::core::Uri templateKey(const std::filesystem::path& path);

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
	lain::flow::serialize::LoadResult loadGraph(const lain::core::Uri& uri,
												const lain::core::Factory<lain::flow::Node>& factory,
												lain::flow::serialize::TemplateCache* cache);

	// Serialize `graph` (+ its canvas `editor` layout) to a data::Value — the same document Save
	// writes, but kept in RAM. The undo history is a stack of these (Save-to-RAM); a restore feeds
	// one back through restoreGraph (Load-from-RAM). Uses sceneCodecs(), so it captures exactly what
	// the on-disk format does — structure, params, node/pin names, dynamic pins, layout — and no
	// runtime-only state (bound boundary values), which is why two snapshots differing only in a bind
	// compare equal. It takes no DocumentOptions, on purpose: they are saved, never undone.
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
