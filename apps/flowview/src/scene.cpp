#include "scene.h"

#include <lain/flow/example/blurnode.h>
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
	static constexpr const char* kBlurKey = "blur";

	void registerExampleNodes(core::Factory<flow::Node>& factory, std::uint32_t size)
	{
		// The nodes are pure CPU (they produce/consume a lain::image::Image), so each
		// creator captures only plain construction values — no device, no std::ref.
		factory.registerType<flow::example::GradientNode>(kGradientKey, size, size);
		factory.registerType<flow::example::TintNode>(kTintKey, 1.0f, 0.5f, 0.5f); // keep R, halve G/B
		factory.registerType<flow::example::BlurNode>(kBlurKey, 2, 1.5f);		   // soft 5x5 Gaussian
	}

	flow::NodeId buildExampleScene(flow::Graph& graph, const core::Factory<flow::Node>& factory)
	{
		// A tiny pipeline: gradient source -> tint transform -> Gaussian blur. Every node's
		// image ports are index 0, so each edge is type-compatible by construction. The blur
		// dogfoods lain::image's operation catalog (convolve) end-to-end through the graph.
		const flow::NodeId gradient = graph.add(factory.create(kGradientKey));
		const flow::NodeId tint = graph.add(factory.create(kTintKey));
		const flow::NodeId blur = graph.add(factory.create(kBlurKey));
		graph.connect(gradient, 0, tint, 0);
		graph.connect(tint, 0, blur, 0);
		return blur; // pull target = the sink; evaluating it pulls the whole chain upstream
	}
} // namespace flowview
