#pragma once

#include <archimedes/archimedes.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace lain::render::testing
{
	// The kinds of device a [gpu] case draws on: the best hardware GPU, and archimedes' CPU device
	// (SwiftShader, staged beside the test).
	enum class DeviceKind
	{
		Hardware,
		Cpu,
	};

	// One instance per test process (ctest runs each case in its own), with a device per kind made on
	// first use. The CPU device's own pool is capped at two threads, since ctest runs several test
	// processes at once.
	struct Gpu
	{
		acm::Instance instance;
		std::array<acm::Device, 2> devices; // after the instance, so they are destroyed first

		Gpu()
		{
			acm::useStagedVulkanRuntime(acm::RuntimeConfig{2});
			instance = acm::Instance("test-render", acm::Version{0, 1, 0});
		}

		std::vector<DeviceKind> kinds() const
		{
			bool hardware = false, cpu = false;
			for (const acm::DeviceOption& option : instance.graphicsOptions())
				(instance.devices()[option.deviceIndex].type == acm::PhysicalDeviceType::Cpu ? cpu : hardware) = true;
			std::vector<DeviceKind> kinds;
			if (hardware)
				kinds.push_back(DeviceKind::Hardware);
			if (cpu)
				kinds.push_back(DeviceKind::Cpu);
			return kinds;
		}

		acm::Device& device(DeviceKind kind)
		{
			acm::Device& device = devices[std::size_t(kind)];
			if (device.valid())
				return device;
			for (const acm::DeviceOption& option : instance.graphicsOptions())
			{
				const bool cpu = instance.devices()[option.deviceIndex].type == acm::PhysicalDeviceType::Cpu;
				if (cpu == (kind == DeviceKind::Cpu))
				{
					device = instance.createDevice(option);
					break;
				}
			}
			return device;
		}
	};

	// The device this run of a [gpu] case draws on. A case that asks runs once per kind of device the
	// machine offers (a Catch2 generator), as archimedes' own tests do: on the hardware GPU, then on
	// the CPU device. Where the CPU driver is staged (LAIN_EXPECT_CPU_DEVICE) a missing CPU device is a
	// failure, not a skip, so a build whose staging broke cannot pass by drawing nothing. Elsewhere a
	// machine with no device SKIPs. Call it once, at the top of the case.
	inline acm::Device& device()
	{
		static Gpu gpu;
		std::vector<DeviceKind> kinds = gpu.kinds();
#ifdef LAIN_EXPECT_CPU_DEVICE
		if (std::find(kinds.begin(), kinds.end(), DeviceKind::Cpu) == kinds.end())
			FAIL("the CPU driver is staged beside this test, but no CPU device is available");
#endif
		if (kinds.empty())
			SKIP("no Vulkan device on this machine");
		const DeviceKind kind = GENERATE_COPY(from_range(kinds));
		UNSCOPED_INFO("device: " << (kind == DeviceKind::Cpu ? "CPU" : "hardware"));
		acm::Device& chosen = gpu.device(kind);
		REQUIRE(chosen.valid());
		return chosen;
	}
} // namespace lain::render::testing
