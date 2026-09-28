#pragma once

// Worker threads for a test: start the process pool, stop it when the case ends.
//
// WHY THIS EXISTS. lain runs ONE pool for the whole process and lain::app owns its lifetime
// (docs/adr/0024-one-process-task-pool.md), so a test — which has no Application — must start it
// itself or get nothing. An unstarted pool is not an error: every dispatch runs INLINE on the
// caller, in dependency order. That is the right default and it is also the trap, because a
// parallel test that forgets to start one exercises the serial path and PASSES, faster. The 100x
// [map],[loop],[group],[scheduler] sweeps are this repo's only race cover, and they would have
// gone green while covering nothing.
//
// So this type exists to make the pair hard to get half-right, and
// "the parallel scheduler really runs on several threads" in libs/flow/test/test_scheduler.cpp
// exists to catch a test that skips it altogether — the failure this cannot see from in here.
//
// It is deliberately NOT a production type. lain::app brackets the pool around its own run loop
// with a plain start/stop pair, and a scope object there would be a second mechanism for a job
// multi's own WorkerPool destructor already does (it asserts, then stops defensively). Tests are
// different only because they have no such owner.
//
// Header-only and test-only: `lain::testing` is built under LAIN_BUILD_TESTING and linked by test
// targets alone.

#include <lain/task/task.h>

namespace lain::testing
{
	// Starts the process pool for as long as this object lives. `threadCount` follows
	// multi::Context::start: -1 (the default) is hardware_concurrency() - 1, and 0 leaves the pool
	// inactive so dispatch runs inline — the deterministic single-threaded execution of a parallel
	// plan, and what `flowview --threads 0` gives a user.
	class ThreadPool
	{
	public:
		explicit ThreadPool(int threadCount = -1)
			: m_owned(task::start(threadCount))
		{
		}

		// Only stops what this object started. Nesting therefore DEFERS rather than cutting the
		// outer scope's pool out from under it: start() refuses a pool that is already running, so
		// an inner ThreadPool owns nothing and its destructor does nothing.
		~ThreadPool()
		{
			if (m_owned)
				task::stop();
		}

		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;

		// True when THIS object started the pool, and so will stop it.
		bool owned() const noexcept { return m_owned; }

	private:
		bool m_owned;
	};
} // namespace lain::testing
