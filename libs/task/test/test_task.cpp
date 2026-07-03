// Smoke test for lain::task — proves the wrapper builds a small task-graph,
// honours dependency edges, and runs it to completion on the executor. This is
// the surface flow's scheduler will lower a node-graph onto.

#include "lain/task/task.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
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
