#include "scene.h"

#include <lain/flow/boundary.h>
#include <lain/flow/example/blurnode.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/example/loadimagenode.h>
#include <lain/flow/example/tintnode.h>
#include <lain/flow/graph.h>
#include <lain/flow/nodes/constant.h> // ConstantNode — sources to drive the control nodes
#include <lain/flow/nodes/gate.h>	  // control nodes over the scene payload (image::Image)
#include <lain/flow/nodes/merge.h>
#include <lain/flow/nodes/select.h>
#include <lain/image/image.h>

namespace flowview
{
	using namespace lain;

	// Registry keys for the example nodes — shared by registration and scene-building
	// so the two can't drift.
	static constexpr const char* kGradientKey = "gradient";
	static constexpr const char* kTintKey = "tint";
	static constexpr const char* kBlurKey = "blur";
	static constexpr const char* kLoadImageKey = "loadimage";
	static constexpr const char* kGateKey = "gate";
	static constexpr const char* kMergeKey = "merge";
	static constexpr const char* kSelectKey = "select";
	static constexpr const char* kConstIntKey = "constInt";
	static constexpr const char* kConstBoolKey = "constBool";
	static constexpr const char* kGroupInputKey = "groupInput";
	static constexpr const char* kGroupOutputKey = "groupOutput";

	const std::vector<NodeCategory>& nodeCatalog()
	{
		// The palette-addable kinds, grouped by role (mirrors the canvasstyle title colours). Keys must
		// match the registrations below. Boundary nodes (GroupInput/GroupOutput) are intentionally NOT
		// here: they're a one-each-per-graph fixture that comes with a New graph and is grown from the
		// Interface panel, not added like an ordinary node.
		static const std::vector<NodeCategory> catalog = {
			{"Sources", {kGradientKey, kLoadImageKey, kConstIntKey, kConstBoolKey}},
			{"Filters", {kTintKey, kBlurKey}},
			{"Control", {kGateKey, kMergeKey, kSelectKey}},
		};
		return catalog;
	}

	void registerExampleNodes(core::Factory<flow::Node>& factory, std::uint32_t size)
	{
		// The nodes are pure CPU (they produce/consume a lain::image::Image), so each
		// creator captures only plain construction values — no device, no std::ref.
		factory.registerType<flow::example::GradientNode>(kGradientKey, size, size);
		factory.registerType<flow::example::TintNode>(kTintKey, 1.0f, 0.5f, 0.5f); // keep R, halve G/B
		factory.registerType<flow::example::BlurNode>(kBlurKey, 2, 1.5f);		   // soft 5x5 Gaussian
		factory.registerType<flow::example::LoadImageNode>(kLoadImageKey);		   // empty path -> set in the gui

		// Control nodes over the scene payload. Gate passes its image when enabled (else suppresses
		// downstream); the variadic Merge/Select are empty at construction — the canvas ± grows their
		// image branches (the "Image" port type, registered in registerSceneSerialization).
		factory.registerType<flow::GateNode<image::Image>>(kGateKey);
		factory.registerType<flow::MergeNode<image::Image>>(kMergeKey);
		factory.registerType<flow::SelectNode<image::Image>>(kSelectKey);

		// Scalar sources to drive the control nodes: a bool for a Gate's `enable`, an int for a
		// Select's `selector`. Their value is a param the inspector edits (checkbox / drag).
		factory.registerType<flow::ConstantNode<int>>(kConstIntKey);
		factory.registerType<flow::ConstantNode<bool>>(kConstBoolKey);

		// The boundary nodes are added to the scene directly (not via the palette), but they must be
		// in the factory too so serialization can name them (keyOf) and recreate them on load.
		factory.registerType<flow::GroupInputNode>(kGroupInputKey);
		factory.registerType<flow::GroupOutputNode>(kGroupOutputKey);
	}

	void buildExampleScene(flow::Graph& graph, const core::Factory<flow::Node>& factory)
	{
		// The M4 boundary pipeline: a host-bound image input -> tint -> Gaussian blur -> a
		// readable image output. Every node's image ports are index 0, so each edge is
		// type-compatible by construction; the blur dogfoods lain::image's operation catalog
		// (convolve) end-to-end. The host binds "source" and reads "result".
		const flow::NodeId in = graph.add<flow::GroupInputNode>();
		static_cast<flow::GroupInputNode&>(graph.node(in)).addBoundary<image::Image>("source");
		const flow::NodeId tint = graph.add(factory.create(kTintKey));
		const flow::NodeId blur = graph.add(factory.create(kBlurKey));
		const flow::NodeId out = graph.add<flow::GroupOutputNode>();
		static_cast<flow::GroupOutputNode&>(graph.node(out)).addBoundary<image::Image>("result");

		graph.connect(in, 0, tint, 0);
		graph.connect(tint, 0, blur, 0);
		graph.connect(blur, 0, out, 0);
	}

	void buildNewScene(flow::Graph& graph)
	{
		// The blank document: just the two boundary nodes, no pins. The user grows the interface (and
		// adds filters between) from there. They're added directly (like the example's boundary nodes),
		// but must also be in the factory so save/load can name them (registerExampleNodes covers that).
		graph.add<flow::GroupInputNode>();
		graph.add<flow::GroupOutputNode>();
	}

	void bindDefaultInput(flow::Graph& graph, std::uint32_t size)
	{
		const auto inputs = graph.boundaryInputs();
		if (inputs.empty())
			return;

		// Generate a gradient image the way the old scene's source did, and inject it as the
		// default binding — so gui-mode has something to show until the Interface panel lands.
		flow::example::GradientNode gradient(size, size);
		gradient.compute();
		if (gradient.output(0).ready())
			inputs[0].setValue(gradient.output(0).value());
	}
} // namespace flowview
