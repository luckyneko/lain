#pragma once

#include <archimedes/archimedes.h>

#include <vector>

namespace lain::render::testing
{
	// The device the [gpu] cases draw on: the first archimedes ranks, made once per test process
	// (ctest runs each case in its own), or nullptr where this machine has none, for the case to SKIP.
	inline acm::Device* device()
	{
		struct Gpu
		{
			acm::Instance instance;
			acm::Device device; // after the instance, so it is destroyed first

			Gpu()
			{
				acm::useStagedVulkanRuntime();
				instance = acm::Instance("test-render", acm::Version{0, 1, 0});
				if (!instance.valid())
					return;
				const std::vector<acm::DeviceOption> options = instance.graphicsOptions();
				if (!options.empty())
					device = instance.createDevice(options.front());
			}
		};
		static Gpu gpu;
		return gpu.device.valid() ? &gpu.device : nullptr;
	}
} // namespace lain::render::testing
