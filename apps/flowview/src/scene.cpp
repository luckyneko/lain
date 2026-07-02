#include "scene.h"

#include <archimedes/acmTypes.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/graph.h>

namespace flowview
{
	using namespace lain;

	// The registry key for the example gradient source — shared by registration and
	// scene-building so the two can't drift.
	static constexpr const char* kGradientKey = "gradient";

	void registerExampleNodes(core::Factory<flow::Node>& factory, acm::Device device, std::uint32_t size)
	{
		const acm::Extent2D extent{size, size};
		// device + extent are captured (by copy) in the creator; each create() builds
		// a fresh GradientNode from them.
		factory.registerType<flow::example::GradientNode>(kGradientKey, device, extent);
	}

	flow::NodeId buildExampleScene(flow::Graph& graph, const core::Factory<flow::Node>& factory)
	{
		return graph.add(factory.create(kGradientKey)); // adopt the factory-built node
	}
} // namespace flowview
