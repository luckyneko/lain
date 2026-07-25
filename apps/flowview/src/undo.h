#pragma once

#include <lain/data/value.h>

#include <cstddef>
#include <vector>

namespace flowview
{
	// A snapshot-based undo/redo history for the graph document. A snapshot is the graph serialized
	// to a data::Value (structure, params, node/pin names, dynamic pins, canvas layout) — the same
	// form Save writes (graphio::snapshotGraph), so undo is Load-from-RAM. Runtime-only state (a
	// bound boundary value — a loaded image, a scalar input) is NOT in the document and so not
	// covered; because push() ignores a snapshot equal to the current one, an edit that touches only
	// such state records no history entry rather than a confusing no-op undo step.
	//
	// The model is a list of full document states with a cursor at the current one. reset() seeds the
	// baseline (a fresh / loaded document — a New or Open resets the history rather than extending
	// it); push() records a new state after an edit, discarding any redone-past tail; undo()/redo()
	// move the cursor and return the state to restore. Bounded to maxDepth states (oldest dropped).
	class UndoStack
	{
	public:
		// Deepest history kept. A snapshot is a small data::Value (a prototyping graph), so this is a
		// generous bound whose only cost is memory; the oldest state is dropped past it.
		static constexpr std::size_t maxDepth = 100;

		// Whether a baseline has been established (reset/push has run at least once).
		bool hasBaseline() const { return !m_states.empty(); }

		// Seed the history with `baseline` as the sole, current state (a new or just-loaded document).
		// Clears any prior history — undo does not cross a New/Open.
		void reset(lain::data::Value baseline);

		// Record `state` as the new current state, unless it equals the current one (a no-op edit, or
		// one that changed only non-serialized runtime state, produces an equal document and is
		// ignored). Discards the redo tail. If no baseline exists yet, this becomes the baseline.
		void push(lain::data::Value state);

		bool canUndo() const { return m_cursor > 0; }
		bool canRedo() const { return hasBaseline() && m_cursor + 1 < m_states.size(); }

		// Step back / forward and return the state to restore. Precondition: canUndo() / canRedo().
		const lain::data::Value& undo();
		const lain::data::Value& redo();

	private:
		std::vector<lain::data::Value> m_states;
		std::size_t m_cursor = 0; // index of the current state within m_states
	};
} // namespace flowview
