// [gpu] integration: the GradientNode example produces a real acm::Texture on its
// output port, through the graph's pull path. We read the texture back and check
// the gradient at known corners. SKIP-aware: no driver / no graphics queue -> the
// test SKIPs (green) rather than failing, matching archimedes' [gpu] suite.

#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <archimedes/archimedes.h>

#include <lain/flow/example/gradientnode.h>
#include <lain/flow/graph.h>

namespace
{
	const acm::GPU* selectGraphicsGpu(const acm::Instance& instance, uint32_t& queueIdx)
	{
		for (const acm::GPU& gpu : instance.getAvailableGPUs())
		{
			for (const acm::GPUQueueFamily& family : gpu.queueFamilies)
			{
				if (family.supportsGraphics)
				{
					queueIdx = family.index;
					return &gpu;
				}
			}
		}
		return nullptr;
	}
}

TEST_CASE("GradientNode emits an uploaded texture on its port", "[flow][gpu]")
{
	acm::Instance instance("lain-flow-tests", acm::Version{ 0, 1, 0, 0 });
	if (!instance.valid())
		SKIP("no Vulkan driver available");

	uint32_t queueIdx = 0;
	const acm::GPU* gpu = selectGraphicsGpu(instance, queueIdx);
	if (!gpu)
		SKIP("no graphics-capable queue family");

	acm::Device device = instance.createDevice(*gpu, queueIdx);
	REQUIRE(device.valid());

	constexpr uint32_t kSize = 64;

	using namespace lain::flow;
	Graph graph;
	const NodeId id = graph.add<example::GradientNode>(device, acm::Extent2D{ kSize, kSize });

	graph.evaluate(id); // pull: runs compute() — creates + uploads the texture

	const Port& out = graph.node(id).output(0);
	REQUIRE(out.value().holds<acm::Texture>());
	acm::Texture texture = out.value().get<acm::Texture>();
	REQUIRE(texture.valid());

	// Read the texture back into a host-visible buffer.
	acm::Buffer readback = device.createBuffer(static_cast<std::size_t>(kSize) * kSize * 4, acm::BufferUsage::TransferDst);
	REQUIRE(readback.valid());
	device.submitSync([&](acm::CommandBuffer cmd) {
		cmd.transitionImage(texture, acm::ImageLayout::ShaderReadOnly, acm::ImageLayout::TransferSrc);
		cmd.copyTextureToBuffer(texture, readback);
	});

	const auto* px = static_cast<const uint8_t*>(readback.map());
	REQUIRE(px != nullptr);

	// R8G8B8A8_Unorm memory order is [R, G, B, A].
	// Top-left (0,0): ramps start at 0; B constant 128.
	CHECK(px[0] == 0);
	CHECK(px[1] == 0);
	CHECK(px[2] == 128);
	CHECK(px[3] == 255);

	// Bottom-right (kSize-1, kSize-1): both ramps at full.
	const std::size_t last = (static_cast<std::size_t>(kSize) * kSize - 1) * 4;
	CHECK(px[last + 0] == 255);
	CHECK(px[last + 1] == 255);
	CHECK(px[last + 2] == 128);
}
