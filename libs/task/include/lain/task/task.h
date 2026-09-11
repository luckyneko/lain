#pragma once

// lain::task — the minimal face of multi that flow's scheduler consumes.
//
// We expose only what lowering a node-graph onto a task-graph needs: a Flow that
// emplaces a task per graph node and wires dependency edges, and an Executor that
// runs one Flow to completion across a work-stealing pool. multi's wider surface
// (async/parallel/each/range, chunk policies, waitAny) is deliberately not
// re-exposed until a caller needs it. No multi:: type appears in a consumer's
// translation unit.
//
// This seam was written over Taskflow and moved to multi (lain's own pool) without
// libs/flow changing by a single line — which is the whole reason it exists. The
// vocabulary is lain's rather than either backend's: a Flow is multi's Recipe, a
// Task is its Step, an Executor is its Context.

#include <multi/context.h>
#include <multi/recipe.h>
#include <multi/recipehandle.h>

#include <cassert>
#include <thread>
#include <utility>

namespace lain::task
{
	class Flow;

	// A handle to one unit of work inside a Flow. Cheap to copy (it indexes into
	// the owning Flow, like the multi::Step it wraps).
	class Task
	{
	public:
		// Run this task before `successor` — i.e. add the edge this -> successor.
		Task& precede(Task successor);

	private:
		friend class Flow;
		Task(Flow& flow, multi::Step step)
			: m_flow(&flow)
			, m_step(step)
		{
		}

		// An edge belongs to the GRAPH, not to either end of it: multi spells one
		// recipe.order(before >> after), so a Task has to be able to reach the Flow that
		// owns it. Holding the pointer here is what keeps precede() on the Task, where
		// every caller already writes it.
		Flow* m_flow{nullptr};
		multi::Step m_step;
	};

	// A task-graph: emplace one Task per node, wire edges with Task::precede,
	// then hand it to an Executor.
	//
	// SINGLE USE — running it consumes the graph, so build a fresh Flow per run.
	// Neither copyable nor movable: the Tasks handed out point back at it, and a move
	// would leave every one of them naming an address that no longer owns the steps.
	class Flow
	{
	public:
		Flow() = default;
		Flow(const Flow&) = delete;
		Flow& operator=(const Flow&) = delete;
		Flow(Flow&&) = delete;
		Flow& operator=(Flow&&) = delete;

		// Add a task running `fn` (any callable). Returns its handle.
		template <typename Fn>
		Task emplace(Fn&& fn)
		{
			const multi::Step step = m_recipe.step(std::forward<Fn>(fn));
			assert(step.valid() && "lain::task::Flow::emplace: the graph is full");
			return Task{*this, step};
		}

	private:
		friend class Task;
		friend class Executor;
		multi::Recipe m_recipe;
	};

	inline Task& Task::precede(Task successor)
	{
		const multi::RecipeResult result = m_flow->m_recipe.order(m_step >> successor.m_step);

		// A DUPLICATE is SUCCESS, and finding that out is what this swap cost. flow emits one
		// plan edge per GRAPH edge, so two inputs of one node fed by one producer ask for the
		// same (step, step) ordering twice — and the second request is already satisfied by the
		// first. Taskflow absorbed that by counting dependents; multi refuses it and keeps the
		// single constraint. The resulting order is identical, which is the only thing an edge
		// in a task graph means. Measured, not assumed: it is exactly what
		// (flowview)a count loop folds a gradient through five blurs produces.
		//
		// A CYCLE or an invalid step is a different matter. multi VALIDATES, where Taskflow
		// would simply have hung; flow::Graph already rejects a cycle when the edge is made, so
		// reaching one here means the lowering invented it — a programming error, asserted.
		assert((result == multi::RecipeResult::Ok || result == multi::RecipeResult::DuplicateEdge) && "lain::task::Task::precede: refused edge (cycle, or a step from another Flow)");
		(void)result; // release builds do not inspect it
		return *this;
	}

	// Work-stealing pool that runs a Flow. Default-sized to the hardware.
	class Executor
	{
	public:
		// `threads` workers. ZERO is meaningful rather than an error: the pool stays
		// inactive and the work runs INLINE on the calling thread, in dependency order —
		// so a parallel plan can be executed deterministically, single-threaded, without a
		// second scheduler. (hardware_concurrency() answering 0, which it may, therefore
		// degrades to correct-but-serial rather than to a made-up thread count.)
		explicit Executor(unsigned threads = std::thread::hardware_concurrency())
		{
			m_context.start(static_cast<int>(threads));
		}

		~Executor() { m_context.stop(); }

		Executor(const Executor&) = delete;
		Executor& operator=(const Executor&) = delete;

		// Run `flow` to completion, blocking until every task has finished, and rethrow the
		// first exception a task let escape.
		//
		// wait() then get() is the pair that does BOTH: multi's get() is a non-blocking fetch
		// that rethrows, and its wait() blocks but deliberately does not. Calling only get()
		// would return the moment the work was still running and swallow the exception with it.
		void run(Flow& flow)
		{
			multi::RecipeHandle handle = m_context.async(std::move(flow.m_recipe));
			handle.wait();
			handle.get();
		}

	private:
		multi::Context m_context;
	};
} // namespace lain::task
