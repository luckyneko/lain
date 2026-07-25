#include "undo.h"

#include <cassert>
#include <cstddef>
#include <utility>

namespace flowview
{
	using namespace lain;

	void UndoStack::reset(data::Value baseline)
	{
		m_states.clear();
		m_states.push_back(std::move(baseline));
		m_cursor = 0;
	}

	void UndoStack::push(data::Value state)
	{
		if (!hasBaseline())
		{
			reset(std::move(state)); // no baseline yet — the first push seeds it
			return;
		}
		if (state == m_states[m_cursor])
			return; // no document change (e.g. a bound-value-only edit) — record nothing

		// A new edit past an undo discards the redo tail, then this state becomes current.
		m_states.erase(m_states.begin() + static_cast<std::ptrdiff_t>(m_cursor) + 1, m_states.end());
		m_states.push_back(std::move(state));
		m_cursor = m_states.size() - 1;

		// Bound the history: drop the oldest states, keeping the cursor pointing at the same one.
		if (m_states.size() > maxDepth)
		{
			const std::size_t drop = m_states.size() - maxDepth;
			m_states.erase(m_states.begin(), m_states.begin() + static_cast<std::ptrdiff_t>(drop));
			m_cursor -= drop;
		}
	}

	const data::Value& UndoStack::undo()
	{
		assert(canUndo());
		--m_cursor;
		return m_states[m_cursor];
	}

	const data::Value& UndoStack::redo()
	{
		assert(canRedo());
		++m_cursor;
		return m_states[m_cursor];
	}
} // namespace flowview
