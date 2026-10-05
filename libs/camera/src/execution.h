#pragma once

#include "lain/camera/method.h"

#include <lain/task/task.h>

#include <cstddef>

// How the camera method modules run independent work (ExecutionPolicy): private to lain::camera, so
// calibration and registration share one answer without either naming the other.
namespace lain::camera::detail
{
	// Run `work(i)` for every i below `count`: in parallel on the process pool normally, serially in
	// order for deterministic debugging. `work` must touch only state of its own index.
	template <typename Work>
	void forEach(std::size_t count, ExecutionPolicy execution, const Work& work)
	{
		if (execution == ExecutionPolicy::DeterministicDebug)
		{
			for (std::size_t i = 0; i < count; ++i)
				work(i);
			return;
		}
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(count), [&work](std::ptrdiff_t i)
					{ work(std::size_t(i)); });
	}
} // namespace lain::camera::detail
