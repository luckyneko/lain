#include "runpolicy.h"

#include <utility>

namespace flowview
{
	std::vector<lain::flow::NodeId> selectionTargets(const lain::flow::EvalPath& drawn,
													 const std::vector<lain::flow::NodeId>& selected)
	{
		if (selected.empty())
			return {};
		if (drawn.empty())
			return selected;
		return {drawn.front().node};
	}

	void RunRequests::runSelection(std::vector<lain::flow::NodeId> targets)
	{
		if (targets.empty())
			return;
		m_selection = std::move(targets);
	}

	void RunRequests::started(const RunScope& scope)
	{
		if (scope.whole())
		{
			clear();
			return;
		}
		m_selection.reset();
	}

	bool RunRequests::due(RunTrigger trigger, bool gestureEnded) const
	{
		return m_runNow || m_selection.has_value() || changeDue(trigger, gestureEnded);
	}

	RunScope RunRequests::scope(RunTrigger trigger, bool gestureEnded) const
	{
		if (m_runNow || changeDue(trigger, gestureEnded) || !m_selection)
			return RunScope{};
		return RunScope{m_selection};
	}

	bool RunRequests::changeDue(RunTrigger trigger, bool gestureEnded) const
	{
		if (!m_changed)
			return false;

		// Exhaustive, so a fourth trigger is a compile error here rather than a silent "never runs".
		switch (trigger)
		{
			case RunTrigger::Live:
				return true;
			case RunTrigger::OnCommit:
				return gestureEnded;
			case RunTrigger::Manual:
				return false; // a change never starts a run — and so never cancels one in flight either
		}
		return false;
	}
} // namespace flowview
