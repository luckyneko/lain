#pragma once

// How a host steers ONE run while it is in flight, and how it sees how far that run has got
// (ADR-0025). The host owns it and passes it to Scheduler::run; any thread may cancel it or read its
// progress while the run holds the evaluation.
//
// SINGLE-USE. A superseded run is cancelled and the run that replaces it gets a fresh control — a
// cancelled control cancels every run it is handed, which is what makes "cancel" mean the run it was
// made for rather than whichever run happens to hold it next. It is not copyable or movable: a run
// and its host share it by address.
//
// What a cancel does is the scheduler's business (scheduler.h): no further node COMPUTES start, the
// crossings between levels still run, a node already computing finishes — or gives up early if it
// asks NodeEvaluation::cancelled() — and the staging loop plans no further stage. Whatever did not
// finish stays stale, so the next run picks it up.
//
// A host may also be TOLD what the run does as it goes (setObserver): when a stage has finished,
// which nodes each stage owes (and which a pull leaves owed), and when each node starts and finishes — WHERE, as an EvalPath, since
// a working evaluation's address names nothing in the copy a host's panes read (M14 slice 7). That is
// how a host learns which nodes are Queued and Computing rather than only how many, and how a node's
// result reaches the screen the moment it lands rather than at the end of its stage.

#include "lain/flow/types.h" // EvalPath, NodeId — where the observer is told each thing happened

#include <atomic>
#include <cstddef>

namespace lain::flow
{
	class NodeRecord;

	// What a host is told as a run goes, through RunControl::setObserver. Every method has an empty
	// default, so a host overrides only what it needs; each states the thread it is called on, and
	// the worker-side ones are noexcept because an exception escaping one would leave inside a step,
	// where the scheduler would read it as that node's compute having thrown.
	//
	// Every report names the node by EvalPath + NodeId: WHICH EVALUATION (the path from the root —
	// a group or loop's interior is {owner, 0}, a map's element i is {map, i}) and which node in it.
	class RunObserver
	{
	public:
		virtual ~RunObserver() = default;

		// BETWEEN stages: after a stage has executed, when another stage follows and the run has not
		// been cancelled — before the next stage is prepared or planned. On the thread that called
		// Scheduler::run, while no step is running, so the evaluation is QUIESCENT and may be read,
		// and copied, from inside it (a PublishedEvaluation is the reason it exists).
		//
		// Never called after the last stage: run() returning is that boundary, and a host already
		// knows when that happens. So a run with no map and no loop — one stage — never calls it. It
		// must not re-enter the scheduler on the same evaluation (the run lease would throw), and
		// whatever it throws propagates out of run() the way a compute's exception does.
		virtual void stageFinished() {}

		// A stage has been planned, and the node at (path, node) is part of what it OWES: its closure
		// request has just been written into the evaluation (Scheduler::Plan::closure), so it stays
		// stale until its own step runs. On the thread that called Scheduler::run, before any step of
		// that stage starts. A node owed again by a later stage is reported again — a loop's frontier
		// every iteration, its interior's nodes once per pass.
		//
		// Mirroring these into a copy is what keeps the copy's staleness sound while results land
		// one node at a time: without it, a node whose upstream has just landed would read as
		// current while it still shows the value it built from the old input.
		virtual void owed(const EvalPath&, NodeId) noexcept {}

		// A PULL (Scheduler::evaluate) leaves the node at (path, node) owed: it is outside the cone
		// the pull was asked for, so this run will never compute it, but something the pull may
		// recompute feeds it — so a recompute request has just been written for it, and it stays stale
		// (Scheduler::Plan::left). On the thread that called Scheduler::evaluate, before any step of
		// that stage starts. Never reported by a full run, whose cone is everything.
		//
		// A copy mirroring the evaluation needs these for the reason it needs owed() — without the
		// mark, a node whose upstream has just landed reads as current while it shows the value it
		// built from the old input — and a host must tell the two apart, because only an owed node is
		// waiting for this run. Showing one of these as Queued would promise a result that never comes.
		virtual void leftOwed(const EvalPath&, NodeId) noexcept {}

		// A node COMPUTE is starting (the node is "Computing"). On the thread that runs it — a pool
		// worker under ParallelScheduler, so several may be in flight at once. Never for a crossing,
		// which only copies values, and never for a compute a cancel skipped.
		virtual void started(const EvalPath&, NodeId) noexcept {}

		// A step has FINISHED, leaving the node at (path, node) as `record` — a copy of its values and
		// bookkeeping, taken by the thread that ran the step, before anything downstream reads it.
		// Called:
		//   * after a node compute that got through, computed or suppressed;
		//   * after one that THREW — the record carries the failure — before the exception leaves;
		//   * after every CROSSING (a group's entry and exit, a map's gather, a loop's fold), for the
		//     node that owns it, with no started() before it;
		//   * with a NULL record for a compute that GAVE UP on a cancel: whatever it wrote is not to be
		//     trusted, and it is still owed.
		// Not called for a compute a cancel skipped. On the thread that ran the step.
		virtual void finished(const EvalPath&, NodeId, const NodeRecord*) noexcept {}
	};

	class RunControl
	{
	public:
		RunControl() = default;
		RunControl(const RunControl&) = delete;
		RunControl& operator=(const RunControl&) = delete;

		// Ask the run to stop. Relaxed, because the flag gates SKIPPING and publishes no data: a step
		// that has not seen it yet simply runs, which is a delay rather than a wrong answer.
		void cancel() noexcept { m_cancelled.store(true, std::memory_order_relaxed); }
		bool cancelled() const noexcept { return m_cancelled.load(std::memory_order_relaxed); }

		// Node computes planned so far. SUMMED ACROSS STAGES, so it grows while the run is in flight
		// — a map's elements and a loop's iterations are planned only once the stage before them has
		// run (ADR-0014, ADR-0021). Crossings are not counted: the count is of the nodes a user sees.
		std::size_t planned() const noexcept { return m_planned.load(std::memory_order_relaxed); }

		// Node computes that got through — computed, or suppressed (ADR-0007). A compute skipped by a
		// cancel, one that gave up, or one that threw is not counted, so finished() never exceeds
		// planned().
		std::size_t finished() const noexcept { return m_finished.load(std::memory_order_relaxed); }

		// Node computes that THREW. Each one is also recorded against its node in the evaluation
		// (Evaluation::failure), so this is how a host tells a throw some node owned — shown where
		// that node is — from one no node did, which it can only report for the run as a whole. More
		// than one is possible: a serial walk stops at the first, but under the pool independent
		// branches keep running and the first exception is the one that leaves run().
		std::size_t failed() const noexcept { return m_failed.load(std::memory_order_relaxed); }

		// Be told what the run does as it goes (RunObserver). Not owned: it must outlive the run. Set it
		// before the run, not during one — the run reads it without a lock. A record is only built
		// when an observer is set, so a run nobody watches pays nothing for it.
		void setObserver(RunObserver* observer) noexcept { m_observer = observer; }

	private:
		friend class Scheduler; // the one writer of the counters, and the one caller of the observer

		std::atomic<bool> m_cancelled{false};
		std::atomic<std::size_t> m_planned{0};
		std::atomic<std::size_t> m_finished{0};
		std::atomic<std::size_t> m_failed{0};
		RunObserver* m_observer = nullptr;
	};
} // namespace lain::flow
