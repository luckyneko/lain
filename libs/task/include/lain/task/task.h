#pragma once

// lain::task — the minimal face of Taskflow that flow's scheduler consumes.
//
// We expose only what lowering a node-graph onto a task-graph needs: a Flow that
// emplaces a task per graph node and wires dependency edges, and an Executor that
// runs one Flow to completion across a work-stealing pool. Taskflow's wider
// surface (subflows, tf::Pipeline, ...) is deliberately not re-exposed until a
// caller needs it. No tf:: type appears in a consumer's translation unit.

#include <taskflow/taskflow.hpp>
#include <thread>
#include <utility>

namespace lain::task
{
	// A handle to one unit of work inside a Flow. Cheap to copy (it indexes into
	// the owning Flow, like the tf::Task it wraps).
	class Task
	{
	public:
		// Run this task before `successor` — i.e. add the edge this -> successor.
		Task& precede(Task successor)
		{
			m_task.precede(successor.m_task);
			return *this;
		}

	private:
		friend class Flow;
		explicit Task(tf::Task task)
			: m_task(task)
		{
		}
		tf::Task m_task;
	};

	// A task-graph: emplace one Task per node, wire edges with Task::precede,
	// then hand it to an Executor.
	class Flow
	{
	public:
		// Add a task running `fn` (any callable). Returns its handle.
		template <typename Fn>
		Task emplace(Fn&& fn)
		{
			return Task{m_flow.emplace(std::forward<Fn>(fn))};
		}

	private:
		friend class Executor;
		tf::Taskflow m_flow;
	};

	// Work-stealing pool that runs a Flow. Default-sized to the hardware.
	class Executor
	{
	public:
		explicit Executor(unsigned threads = std::thread::hardware_concurrency())
			: m_exec(threads ? threads : 1u)
		{
		}

		// Run `flow` to completion, blocking until every task has finished.
		void run(Flow& flow) { m_exec.run(flow.m_flow).wait(); }

	private:
		tf::Executor m_exec;
	};
} // namespace lain::task
