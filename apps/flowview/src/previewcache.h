#pragma once

#include "pinkey.h"

#include <lain/flow/portvalue.h> // flow::PortValue (the source an entry was made from)
#include <lain/gui/context.h>	 // gui::Texture (a member of the map)

#include <map>

namespace lain::flow
{
	class Evaluation;
	class Graph;
} // namespace lain::flow

namespace flowview
{
	class ValueViews;

	// One uploaded GPU thumbnail per ready VIEWABLE port, keyed by its stable PinKey. The panes
	// (Inspector / Interface / Preview) draw from it; only edits refresh it, because
	// acm::Texture::upload is a synchronous, stalling submit that must not run every frame.
	//
	// And a refresh re-produces and re-uploads only the ports whose PAYLOAD changed. A payload is
	// immutable, so the same payload is the same picture: each entry keeps the value it was made
	// from and asks PortValue::samePayload of the port's current one. Without that, every frame of a
	// param drag re-uploaded every image on the level, though the drag changed one node and what is
	// downstream of it. Keeping the source also holds its payload alive until the next refresh,
	// which is what makes an identity comparison sound — a freed payload's address could otherwise
	// be reused by a new one and look unchanged.
	//
	// What "viewable" means is the ValueViews registry's answer, not this class's: a port's value is
	// handed to it and what comes back is the still image that stands for that value — the image
	// itself for an image::Image (aliased, so nothing is copied), a decoded poster frame for a
	// frame sequence. A type with no view registered simply has no thumbnail.
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

		// Rebuild the cache from the graph iff it was marked dirty: upsert a thumbnail per port whose
		// value `views` can poster (upload in place when the size/format matches, else reallocate via
		// `ctx`) — skipping a port whose payload is the one its thumbnail was made from — and prune
		// thumbnails whose port is gone (the erased gui::Texture reclaims its descriptor). `path` is the level `graph` sits at — it goes into every key this builds, so
		// entries from two levels (or two evaluations of one definition) can never be mistaken for
		// each other.
		//
		// Producing a poster may DECODE (a sequence's is its first frame), which is affordable only
		// because this runs on an edit and not per frame — the same reason the upload is deferred.
		void refreshIfDirty(const GraphPath& path, const lain::flow::Graph& graph,
							const lain::flow::Evaluation& evaluation, const ValueViews& views,
							lain::gui::Context& ctx);

		// The valid thumbnail for `key`, or nullptr when there is none (missing or not yet
		// uploaded) — folds the "found and valid" check the panes all repeat.
		const lain::gui::Texture* find(const PinKey& key) const;

		// Drop every cached thumbnail (on graph replace / shutdown). Must run while the owning
		// Context's backend is alive so the descriptors are reclaimed cleanly.
		void clear() { m_entries.clear(); }

	private:
		// A thumbnail and the port value it was made from — the value, not the poster, since a
		// poster may be produced fresh each time (a sequence's is a decode) and so never compares
		// the same.
		struct Entry
		{
			lain::flow::PortValue source;
			lain::gui::Texture texture;
		};

		std::map<PinKey, Entry> m_entries;
		bool m_dirty = true; // rebuild on the next refreshIfDirty (init + after edits)
	};
} // namespace flowview
