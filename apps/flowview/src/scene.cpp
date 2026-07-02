#include "scene.h"

#include <archimedes/acmTypes.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/example/tintnode.h>
#include <lain/flow/graph.h>

namespace flowview
{
	using namespace lain;

	// Registry keys for the example nodes — shared by registration and scene-building
	// so the two can't drift.
	static constexpr const char* kGradientKey = "gradient";
	static constexpr const char* kTintKey = "tint";

	void registerExampleNodes(core::Factory<flow::Node>& factory, acm::Device device, std::uint32_t size)
	{
		const acm::Extent2D extent{size, size};
		// Construction context is captured (by copy) in each creator; every create()
		// builds a fresh node from it.
		factory.registerType<flow::example::GradientNode>(kGradientKey, device, extent);
		factory.registerType<flow::example::TintNode>(kTintKey, device, 1.0f, 0.5f, 0.5f); // keep R, halve G/B
	}

	flow::NodeId buildExampleScene(flow::Graph& graph, const core::Factory<flow::Node>& factory)
	{
		// A tiny pipeline: the gradient source feeds a tint transform. Both nodes' texture
		// ports are index 0, so the edge is type-compatible by construction.
		const flow::NodeId gradient = graph.add(factory.create(kGradientKey));
		const flow::NodeId tint = graph.add(factory.create(kTintKey));
		graph.connect(gradient, 0, tint, 0);
		return tint; // pull target = the sink; evaluating it pulls the gradient upstream
	}
} // namespace flowview
