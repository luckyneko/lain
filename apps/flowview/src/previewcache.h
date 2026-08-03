#pragma once

#include "pinkey.h"

#include <lain/gui/context.h> // gui::Texture (a member of the map)

#include <map>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

namespace flowview
{
	// One uploaded GPU thumbnail per ready image port, keyed by its stable PinKey. The panes
	// (Inspector / Interface / Preview) draw from it; only edits refresh it, because
	// acm::Texture::upload is a synchronous, stalling submit that must not run every frame.
	//
	// Ownership: the MainWindow owns one of these and a lain::gui::Context; refresh() borrows
	// the context to (re)allocate descriptors. The cached gui::Texture handles must be released
	// (clear()) while that Context's ImGui backend is still alive.
	class PreviewCache
	{
	public:
		// Mark the cache stale so the next refreshIfDirty() rebuilds it (init + after any edit
		// that may have changed images / added / removed ports).
		void markDirty() { m_dirty = true; }

		// Rebuild the cache from the graph iff it was marked dirty: upsert a thumbnail per ready
		// image port (upload in place when the size/format matches, else reallocate via `ctx`) and
		// prune thumbnails whose port is gone (the erased gui::Texture reclaims its descriptor).
		// `path` is the level `graph` sits at — it goes into every key this builds, so entries from
		// two levels (or two evaluations of one definition) can never be mistaken for each other.
		void refreshIfDirty(const GraphPath& path, const lain::flow::Graph& graph,
							const lain::flow::Evaluation& evaluation, lain::gui::Context& ctx);

		// The valid thumbnail for `key`, or nullptr when there is none (missing or not yet
		// uploaded) — folds the "found and valid" check the panes all repeat.
		const lain::gui::Texture* find(const PinKey& key) const;

		// Drop every cached thumbnail (on graph replace / shutdown). Must run while the owning
		// Context's backend is alive so the descriptors are reclaimed cleanly.
		void clear() { m_textures.clear(); }

	private:
		std::map<PinKey, lain::gui::Texture> m_textures;
		bool m_dirty = true; // rebuild on the next refreshIfDirty (init + after edits)
	};
} // namespace flowview
