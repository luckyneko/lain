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
// A host may also ask to be told when a STAGE has finished (setStageObserver): the one point inside
// a run where the evaluation is quiescent, so a host can copy it and show a map's or a loop's
// progress before the whole run returns.
//
// (A per-step started/finished hook joins this with M14 slice 7, which is where a host learns WHICH
// nodes are Queued and Computing rather than only how many.)

#include <atomic>
#include <cstddef>
#include <functional>
#include <utility>

namespace lain::flow
{
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

		// Called BETWEEN stages: after a stage has executed, when another stage follows and the run
		// has not been cancelled — before the next stage is prepared or planned. It runs on the thread
		// that called Scheduler::run, while no step is running, so the evaluation is QUIESCENT and may
		// be read, and copied, from inside it (a PublishedEvaluation is the reason it exists).
		//
		// Never called after the last stage: run() returning is that boundary, and a host already
		// knows when that happens. So a run with no map and no loop — one stage — never calls it.
		//
		// Set it before the run, not during one: it is read by the run without a lock. It must not
		// re-enter the scheduler on the same evaluation (the run lease would throw), and whatever it
		// throws propagates out of run() the way a compute's exception does.
		void setStageObserver(std::function<void()> observer) { m_stageObserver = std::move(observer); }

	private:
		friend class Scheduler; // the one writer of the counters, and the one caller of the observer

		void notifyStage() const
		{
			if (m_stageObserver)
				m_stageObserver();
		}

		std::atomic<bool> m_cancelled{false};
		std::atomic<std::size_t> m_planned{0};
		std::atomic<std::size_t> m_finished{0};
		std::atomic<std::size_t> m_failed{0};
		std::function<void()> m_stageObserver;
	};
} // namespace lain::flow
