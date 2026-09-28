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
// (A per-step started/finished hook joins this with M14 slice 7, which is where a host learns WHICH
// nodes are Queued and Computing rather than only how many.)

#include <atomic>
#include <cstddef>

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

	private:
		friend class Scheduler; // the one writer of the counters

		std::atomic<bool> m_cancelled{false};
		std::atomic<std::size_t> m_planned{0};
		std::atomic<std::size_t> m_finished{0};
	};
} // namespace lain::flow
