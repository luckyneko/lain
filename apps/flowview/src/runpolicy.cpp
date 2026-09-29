#include "runpolicy.h"

namespace flowview
{
	bool RunRequests::due(RunTrigger trigger, bool gestureEnded) const
	{
		if (m_runNow)
			return true;
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
