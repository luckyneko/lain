// Smoke test for lain::task — proves the wrapper builds a small task-graph,
// honours dependency edges, and runs it to completion on the executor. This is
// the surface flow's scheduler will lower a node-graph onto.

#include "lain/task/task.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

TEST_CASE("lain::task runs a flow and honours edges", "[task]")
{
	lain::task::Flow flow;

	std::vector<int> order;
	std::atomic<int> ran{0};

	auto a = flow.emplace([&]
						  { order.push_back(1); ++ran; });
	auto b = flow.emplace([&]
						  { order.push_back(2); ++ran; });
	auto c = flow.emplace([&]
						  { order.push_back(3); ++ran; });

	// a -> b -> c : a strictly serial chain, so order is deterministic.
	a.precede(b);
	b.precede(c);

	lain::task::Executor exec;
	exec.run(flow);

	REQUIRE(ran.load() == 3);
	REQUIRE(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("an executor with no workers runs inline, in dependency order", "[task]")
{
	// Zero is a capability rather than an error: multi leaves the pool inactive and dispatches
	// on the caller. That makes a parallel plan executable deterministically and single-threaded
	// with no second scheduler — and it is what stops a platform whose hardware_concurrency()
	// answers 0 from getting a made-up thread count instead.
	//
	// The ORDER is the assertion that matters. "It ran" would pass just as well if inline
	// dispatch ignored the graph and fired the steps in the order they were added, which for a
	// reversed chain is exactly the wrong answer.
	lain::task::Flow flow;
	std::vector<int> order;
	std::vector<std::thread::id> ranOn;

	auto third = flow.emplace([&]
							  { order.push_back(3); ranOn.push_back(std::this_thread::get_id()); });
	auto second = flow.emplace([&]
							   { order.push_back(2); ranOn.push_back(std::this_thread::get_id()); });
	auto first = flow.emplace([&]
							  { order.push_back(1); ranOn.push_back(std::this_thread::get_id()); });

	// Declared 3, 2, 1 and ordered 1 -> 2 -> 3, so insertion order and dependency order disagree.
	first.precede(second);
	second.precede(third);

	lain::task::Executor exec{0};
	exec.run(flow);

	REQUIRE(order == std::vector<int>{1, 2, 3});

	// INLINE is the other half of the claim, and asserting the order alone would not reach it:
	// the order is 1, 2, 3 whether or not a worker ran it. Every step must have run on the
	// CALLER's thread — which is also why the unsynchronised push_back above is safe here.
	REQUIRE(ranOn.size() == 3);
	for (const std::thread::id& id : ranOn)
		REQUIRE(id == std::this_thread::get_id());
}

TEST_CASE("a task's exception escapes the run", "[task]")
{
	// multi's get() rethrows but does not block, and its wait() blocks but does not rethrow.
	// Executor::run does both, in that order; calling only get() would return while the work
	// was still running and swallow the exception with it. flow relies on this — a compute()
	// that throws must leave its node stale and reach the caller, not vanish into a worker.
	lain::task::Flow flow;
	flow.emplace([]
				 { throw std::runtime_error("boom"); });

	lain::task::Executor exec;
	REQUIRE_THROWS_AS(exec.run(flow), std::runtime_error);
}

TEST_CASE("a duplicate edge is the constraint that is already there", "[task]")
{
	// flow emits one plan edge per GRAPH edge, so two inputs of one node fed by one producer
	// ask for the same ordering twice. multi refuses the second where Taskflow counted it; the
	// resulting order is identical, which is the only thing an edge in a task graph means.
	// Asserting this here is what keeps precede() from treating it as a programming error again.
	lain::task::Flow flow;
	std::vector<int> order;

	auto a = flow.emplace([&]
						  { order.push_back(1); });
	auto b = flow.emplace([&]
						  { order.push_back(2); });

	a.precede(b);
	a.precede(b); // the same constraint, asked for twice

	lain::task::Executor exec;
	exec.run(flow);

	REQUIRE(order == std::vector<int>{1, 2});
}
