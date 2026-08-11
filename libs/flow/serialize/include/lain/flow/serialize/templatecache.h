#pragma once

#include "lain/flow/serialize/loadresult.h" // EditorTree — a template's layout travels with it

#include <lain/flow/graph.h>

#include <map>
#include <memory>
#include <string>

// One definition per template (ADR-0013). A document may hold several linked groups built from one
// file; without a cache each of them loads that file separately and owns a private copy of the same
// recipe. With one, they share a single `const Graph` and differ only in their runtime state — which
// is the arrangement ADR-0012 made safe by moving values out of the definition and into a host-owned
// Evaluation.
//
// The TYPE lives here because resolution does: the recursive load and the self-link cycle guard are
// flow::serialize's, and this is what they consult. The INSTANCE belongs to the HOST, because the
// events that make an entry wrong — saving a document, an explicit reload, opening a different
// document — are only visible there. flow does no file I/O and has no notion of document lifetime.
namespace lain::flow::serialize
{
	class TemplateCache
	{
	public:
		// A template as loaded: its definition, and the layout its author arranged. The layout travels
		// with the definition because loading a template produces both, and an instance that discards
		// it drops its nodes into default columns (which is exactly what M5's bug eight was).
		struct Entry
		{
			std::shared_ptr<const Graph> definition;
			EditorTree editor;
		};

		// `key` is the resolver's CANONICAL key (an absolute path, typically) — the same string the
		// cycle guard compares, so two spellings of one file cannot become two definitions.
		const Entry* find(const std::string& key) const
		{
			const auto it = m_entries.find(key);
			return it == m_entries.end() ? nullptr : &it->second;
		}

		// Stored only once a template has finished loading — never while it is being built, or a link
		// cycle would be half-recorded instead of refused.
		void store(std::string key, Entry entry) { m_entries[std::move(key)] = std::move(entry); }

		// Drop one template. A live instance keeps working: it holds a shared_ptr, so invalidation
		// affects only what the NEXT load resolves to.
		void invalidate(const std::string& key) { m_entries.erase(key); }

		// Drop everything — the reload gesture, and a document swap.
		void clear() { m_entries.clear(); }

		std::size_t size() const { return m_entries.size(); }

	private:
		std::map<std::string, Entry> m_entries;
	};
} // namespace lain::flow::serialize
