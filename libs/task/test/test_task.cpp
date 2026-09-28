// lain::task is a namespace ALIAS for multi, so there is no wrapper here to test. What these
// cases pin are lain's ASSUMPTIONS ABOUT MULTI — the three facts flow's lowering and lain::app's
// lifetime are built on, each of which multi is free to change and none of which multi's own
// suite owes lain:
//
//   1. a duplicate edge is success, not a refusal to report;
//   2. a step's exception reaches the caller, through waitAll + get and only through both;
//   3. an unstarted pool runs the plan inline on the CALLING thread, in dependency order.
//
// Characterisation tests of a dependency, in other words, and deliberately so: each one fails
// loudly at the seam rather than as a wrong answer somewhere in flow.

#include "lain/task/task.h"

#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <thread>
#include <vector>

// These cases sit at global scope, so they cannot reach `task::` the way lain's own namespaces do.
// The alias is what keeps them spelling the types the one way lain spells them.
namespace task = lain::task;

// The whole of what flow's lowering does once the steps are wired: dispatch, participate in the
// stealing while waiting, then fetch — which rethrows.
static void runToCompletion(task::Recipe&& recipe)
{
	task::RecipeHandle handle = task::async(std::move(recipe));
	task::waitAll(handle);
	handle.get();
}

TEST_CASE("a duplicate edge is the constraint that is already there", "[task]")
{
	// flow emits one plan edge per GRAPH edge, so two inputs of one node fed by one producer ask
	// for the same ordering twice. multi refuses the second and keeps the single constraint; the
	// resulting order is identical, which is the only thing an edge in a task graph means. This
	// is what stops the lowering's assert from calling it a programming error again.
	lain::testing::ThreadPool pool;
	task::Recipe recipe;
	std::vector<int> order;

	const task::Step a = recipe.step([&]
									 { order.push_back(1); });
	const task::Step b = recipe.step([&]
									 { order.push_back(2); });

	REQUIRE(recipe.order(a >> b) == task::RecipeResult::Ok);
	REQUIRE(recipe.order(a >> b) == task::RecipeResult::DuplicateEdge); // the same one, asked twice

	runToCompletion(std::move(recipe));
	REQUIRE(order == std::vector<int>{1, 2});
}

TEST_CASE("a step's exception escapes the run", "[task]")
{
	// get() rethrows but does NOT block; waitAll() blocks but does not rethrow. Both, in that
	// order. Calling only get() returns while the work is still running and swallows the
	// exception with it — flow relies on the opposite, since a compute() that throws must leave
	// its node stale and reach the caller rather than vanish into a worker.
	//
	// THE POOL IS WHAT MAKES THIS CASE MEAN ANYTHING. With no workers the recipe finishes inline
	// during async(), so the handle is already complete and dropping the wait would still rethrow
	// — the case would pass with the bug in it. Only real workers put the throw in flight.
	lain::testing::ThreadPool pool;
	task::Recipe recipe;
	recipe.step([]
				{ throw std::runtime_error("boom"); });

	REQUIRE_THROWS_AS(runToCompletion(std::move(recipe)), std::runtime_error);
}

TEST_CASE("an unstarted pool runs inline, in dependency order", "[task]")
{
	// No ThreadPool here on purpose. An inactive pool is not an error: multi dispatches on the
	// caller, so a parallel plan is executable deterministically and single-threaded with no
	// second scheduler. That is what `--threads 0` gives a user, and what a platform whose
	// hardware_concurrency() answers 0 degrades to instead of a made-up thread count.
	//
	// The ORDER is the assertion that matters. "It ran" would pass just as well if inline
	// dispatch ignored the graph and fired the steps as they were added, which for a reversed
	// chain is exactly the wrong answer.
	task::Recipe recipe;
	std::vector<int> order;
	std::vector<std::thread::id> ranOn;

	const task::Step third = recipe.step([&]
										 { order.push_back(3); ranOn.push_back(std::this_thread::get_id()); });
	const task::Step second = recipe.step([&]
										  { order.push_back(2); ranOn.push_back(std::this_thread::get_id()); });
	const task::Step first = recipe.step([&]
										 { order.push_back(1); ranOn.push_back(std::this_thread::get_id()); });

	// Declared 3, 2, 1 and ordered 1 -> 2 -> 3, so insertion order and dependency order disagree.
	recipe.order(first >> second);
	recipe.order(second >> third);

	runToCompletion(std::move(recipe));

	REQUIRE(order == std::vector<int>{1, 2, 3});

	// INLINE is the other half of the claim, and the order alone does not reach it: the order is
	// 1, 2, 3 whether or not a worker ran it. Every step must have run on the CALLER's thread —
	// which is also what makes the unsynchronised push_back above safe.
	REQUIRE(ranOn.size() == 3);
	for (const std::thread::id& id : ranOn)
		REQUIRE(id == std::this_thread::get_id());
}
