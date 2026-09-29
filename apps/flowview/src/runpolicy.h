#pragma once

// The host's RUN POLICY (M14 slices 5 and 8, ADR-0025): WHEN a run starts, THROUGH WHAT, and HOW FAR
// it reaches. The runner only runs (runner.h); deciding that a run is due, and what it covers, is this
// file's, and it is driver-free so the rule can be tested without a window, a device or a thread.

#include <lain/flow/types.h> // NodeId, EvalPath — what a Run Selection names

#include <optional>
#include <vector>

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

	// How far a run reaches. The whole stale closure — what a change or an explicit Run asks for — or,
	// for a RUN SELECTION, only the part of it in the upstream cone of chosen targets
	// (flow::Scheduler::evaluate): "get me this far" on a graph too slow to run whole. What a partial
	// run does not reach stays Stale.
	struct RunScope
	{
		// Absent: the whole closure. Present: the targets, NodeIds of the ROOT level — a pull plans
		// from the root, so a selection deeper down is mapped onto one first (selectionTargets).
		std::optional<std::vector<lain::flow::NodeId>> targets;

		bool whole() const { return !targets.has_value(); }
	};

	// The root-level targets a Run Selection of `selected` means, on the level `drawn` is showing. At
	// the root, the selection itself. Inside a group (any kind — a map's element and a loop's body
	// alike), the root-level node the path descends through: that node runs WHOLE, its interior's own
	// stale closure and not only the selected part of it. Restricting each level to its own cone is
	// possible and deferred — trigger: a slow group interior where "get me this far" matters — and a
	// loop would still have to run whole, since a body run in part breaks its carries. Nothing selected
	// means nothing to run.
	std::vector<lain::flow::NodeId> selectionTargets(const lain::flow::EvalPath& drawn,
													 const std::vector<lain::flow::NodeId>& selected);

	// What has asked for a run and not had one yet, and the rule that turns that into "start one now" —
	// and into how far that run reaches.
	//
	// Three kinds of ask, because the triggers treat them differently: a CHANGE (an edit, a binding, a
	// group sync, a document swap — anything after which the shown values may no longer match), an
	// explicit RUN, and a RUN SELECTION. A change is due at once under Live, once the gesture ends under
	// On commit, and never under Manual; a Run and a Run Selection are due under every trigger.
	//
	// A due change or a Run asks for the WHOLE closure, which covers any selection, so it wins. A
	// partial run answers only the selection ask: a change it did not cover still asks, so under Live
	// or On commit that change supersedes the partial run with a whole one — Live means every change
	// runs everything — while under Manual it waits, as every change does there.
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

		// A Run Selection of `targets` (already root-level — selectionTargets). Replaces an earlier one
		// not yet started: the newest selection is the one meant. Empty targets ask for nothing.
		void runSelection(std::vector<lain::flow::NodeId> targets);

		// Stop: forget every ask. What the stopped run finished is kept; the rest waits for the next.
		void clear()
		{
			m_changed = false;
			m_runNow = false;
			m_selection.reset();
		}

		// A run started over the NEWEST document, reaching `scope`. A whole run answers every ask; a
		// partial one only the selection it was. (A change made after this, while the run is in flight,
		// asks again.)
		void started(const RunScope& scope);

		// Whether a run should start now under `trigger`. `gestureEnded` is the undo history's own
		// boundary — no widget active — so a committed edit means the same thing to both.
		bool due(RunTrigger trigger, bool gestureEnded) const;

		// How far the run that is due reaches: WHOLE when Run was asked or a change is due, else the
		// selection's targets. Meaningful only when due() answers true.
		RunScope scope(RunTrigger trigger, bool gestureEnded) const;

	private:
		// Whether the CHANGE ask alone is due under `trigger` — the triggers' own rule.
		bool changeDue(RunTrigger trigger, bool gestureEnded) const;

		bool m_changed = false;
		bool m_runNow = false;
		std::optional<std::vector<lain::flow::NodeId>> m_selection;
	};
} // namespace flowview
