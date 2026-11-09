#pragma once

#include "lain/camera/method.h"

#include <lain/task/task.h>

#include <cstddef>

// How the camera method modules run independent work (ExecutionPolicy): private to lain::camera, so
// calibration and registration share one answer without either naming the other.
namespace lain::camera::detail
{
	// Run `work(i)` for every i below `count`: in parallel on the process pool normally, serially in
	// order for deterministic debugging. `work` must touch only state of its own index. Named and
	// ordered as C++26's std::execution::bulk is (policy, count, work), though this one blocks until
	// every call has finished.
	template <typename Work>
	void bulk(ExecutionPolicy execution, std::size_t count, const Work& work)
	{
		task::range(execution == ExecutionPolicy::DeterministicDebug ? task::Serial : task::PerItem, std::size_t{0}, count, std::size_t{1}, work);
	}
} // namespace lain::camera::detail
