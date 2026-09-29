#pragma once

// The host's RUN POLICY (M14 slice 5, ADR-0025): WHEN a run starts, and THROUGH WHAT. The runner only
// runs (runner.h); deciding that a run is due is this file's, and it is driver-free so the rule can be
// tested without a window, a device or a thread.

namespace flowview
{
	// What starts a run, chosen per DOCUMENT — a choice about how expensive the graph is, which is why
	// it travels with the file rather than with the session. Never switched automatically.
	enum class RunTrigger
	{
		Live,	  // every change, including each frame of a drag — the default
		OnCommit, // when a gesture ends: a slider drag is one run, on release
		Manual,	  // only an explicit Run
	};

	// Which scheduler a run goes through. Chosen per SESSION; takes effect at the next run.
	enum class RunStrategy
	{
		Parallel, // the process task pool — the default
		Serial,	  // one topo-order walk on the coordinator
	};

	// What has asked for a run and not had one yet, and the rule that turns that into "start one now".
	//
	// Two kinds of ask, because the triggers treat them differently: a CHANGE (an edit, a binding, a
	// group sync, a document swap — anything after which the shown values may no longer match) and an
	// explicit RUN. A change is due at once under Live, once the gesture ends under On commit, and never
	// under Manual; a Run is due under every trigger.
	//
	// Nothing is latched from one frame to the next except the asks themselves: due() is asked afresh
	// each frame. So under On commit, a new drag that begins before a superseded run has drained holds
	// the next run until ITS release — which is what On commit means. And a change owed under Manual is
	// not lost: switching to Live makes it due.
	class RunRequests
	{
	public:
		// Something changed that the shown values may not reflect yet.
		void changed() { m_changed = true; }

		// An explicit Run.
		void runNow() { m_runNow = true; }

		// Stop: forget both. What the stopped run finished is kept; the rest waits for the next ask.
		void clear()
		{
			m_changed = false;
			m_runNow = false;
		}

		// A run started over the NEWEST document, so it answers both asks. (A change made after this,
		// while the run is in flight, asks again.)
		void started() { clear(); }

		// Whether a run should start now under `trigger`. `gestureEnded` is the undo history's own
		// boundary — no widget active — so a committed edit means the same thing to both.
		bool due(RunTrigger trigger, bool gestureEnded) const;

	private:
		bool m_changed = false;
		bool m_runNow = false;
	};
} // namespace flowview
