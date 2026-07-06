#include "scene.h"

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

	void registerExampleNodes(core::Factory<flow::Node>& factory, std::uint32_t size)
	{
		// The nodes are pure CPU (they produce/consume a lain::image::Image), so each
		// creator captures only plain construction values — no device, no std::ref.
		factory.registerType<flow::example::GradientNode>(kGradientKey, size, size);
		factory.registerType<flow::example::TintNode>(kTintKey, 1.0f, 0.5f, 0.5f); // keep R, halve G/B
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
