#include "scene.h"

#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/example/blurnode.h>
#include <lain/flow/example/clipsequencenode.h>
#include <lain/flow/example/combinenode.h>
#include <lain/flow/example/convertnode.h>
#include <lain/flow/example/frameatnode.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/example/listdirnode.h>
#include <lain/flow/example/loadimagenode.h>
#include <lain/flow/example/opensequencenode.h>
#include <lain/flow/example/tintnode.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/nodes/constant.h> // ConstantNode — sources to drive the control nodes
#include <lain/flow/nodes/gate.h>	  // control nodes over the scene payload (image::Image)
#include <lain/flow/nodes/merge.h>
#include <lain/flow/nodes/select.h>
#include <lain/flow/scheduler.h>
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
	static constexpr const char* kConstFloatKey = "constFloat";
	static constexpr const char* kConstPathKey = "constPath";
	static constexpr const char* kConstStringKey = "constString";
	static constexpr const char* kGroupInputKey = "groupInput";
	static constexpr const char* kGroupOutputKey = "groupOutput";
	static constexpr const char* kListDirKey = "listDir";
	static constexpr const char* kCombineKey = "combine";
	static constexpr const char* kConvertKey = "convert";

	// The frame-sequence kinds (M10). In the catalog since slice 7 — until then they were registered
	// in the factory alone and reachable only from the cli, the same order M8 used when the map node
	// ran headless a slice before it appeared in a menu.
	static constexpr const char* kOpenSequenceKey = "openSequence";
	static constexpr const char* kFrameAtKey = "frameAt";
	static constexpr const char* kClipSequenceKey = "clipSequence";
	static constexpr const char* kGroupKey = "group";
	static constexpr const char* kMapKey = "map";
	static constexpr const char* kLinkedGroupKey = "linkedGroup";

	const std::vector<NodeCategory>& nodeCatalog()
	{
		// The palette-addable kinds, grouped by role (mirrors the canvasstyle title colours). Keys must
		// match the registrations below. Boundary nodes (GroupInput/GroupOutput) are intentionally NOT
		// here: they're a one-each-per-graph fixture that comes with a New graph and is grown from the
		// Interface panel, not added like an ordinary node.
		static const std::vector<NodeCategory> catalog = {
			{"Sources", {kGradientKey, kLoadImageKey, kListDirKey, kConstIntKey, kConstBoolKey, kConstFloatKey, kConstPathKey, kConstStringKey}},
			{"Filters", {kTintKey, kBlurKey, kCombineKey, kConvertKey}},
			// Footage. openSequence brings a folder of stills or a video file in as one sequence
			// (medium-neutral — the opener seam decides which), frameAt is where it becomes pixels a
			// graph can process, and clipSequence trims the range. A category of their own rather
			// than a source + two filters: what they have in common is the payload they pass, which
			// is what a user is looking for when reaching for them.
			{"Sequence", {kOpenSequenceKey, kFrameAtKey, kClipSequenceKey}},
			{"Control", {kGateKey, kMergeKey, kSelectKey}},
			// A group is added empty (its inner graph is born with its own boundary pair) and grown by
			// descending into it, and a MAP the same way — the difference is only that a map's face is
			// lifted, so its `files` input takes the whole collection its interior is written against
			// one element of. A LINKED group needs a template chosen first, so the menu bar adds it
			// through a file dialog rather than from this list.
			{"Groups", {kGroupKey, kMapKey}},
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

		// The two ends of a MAP (M8): a collection source, and the reduce that consumes one.
		// ListDir is what makes a map's arity data-dependent — nothing can know how many files a
		// folder holds until it has run, which is why the scheduler plans in stages.
		factory.registerType<flow::example::ListDirNode>(kListDirKey);
		factory.registerType<flow::example::CombineNode>(kCombineKey);
		// The way a graph complies with the video writer's refusals: declare an untagged image and
		// convert it to something a container can state (WORK.md M10 slice 6c).
		factory.registerType<flow::example::ConvertNode>(kConvertKey);
		factory.registerType<flow::example::OpenSequenceNode>(kOpenSequenceKey);
		factory.registerType<flow::example::FrameAtNode>(kFrameAtKey);
		factory.registerType<flow::example::ClipSequenceNode>(kClipSequenceKey);

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

		// Sources for the settings a node exposes as INPUTS rather than only params (see
		// the input-default mechanism (flow::Default)): a folder for ListDir, an extension filter for it. Wiring one is how a
		// setting becomes part of the graph — drivable from a boundary input, and visible on the
		// canvas — instead of being buried in a node's inspector.
		factory.registerType<flow::ConstantNode<float>>(kConstFloatKey); // drives a Blur's sigma
		factory.registerType<flow::ConstantNode<std::filesystem::path>>(kConstPathKey);
		factory.registerType<flow::ConstantNode<std::string>>(kConstStringKey);

		// The boundary nodes are added to the scene directly (not via the palette), but they must be
		// in the factory too so serialization can name them (keyOf) and recreate them on load.
		factory.registerType<flow::GroupInputNode>(kGroupInputKey);
		factory.registerType<flow::GroupOutputNode>(kGroupOutputKey);

		// Group nodes. Both must be in the factory for serialization to name them; only the inline one
		// is palette-addable (a linked group is created by picking its template).
		factory.registerType<flow::InlineGroupNode>(kGroupKey);
		factory.registerType<flow::LinkedGroupNode>(kLinkedGroupKey);
		factory.registerType<flow::MapNode>(kMapKey);
	}

	void buildExampleScene(flow::Graph& graph, const core::Factory<flow::Node>& factory)
	{
		// The M4 boundary pipeline: a host-bound image input -> tint -> Gaussian blur -> a
		// readable image output. Every node's image ports are index 0, so each edge is
		// type-compatible by construction; the blur dogfoods lain::image's operation catalog
		// (convolve) end-to-end. The host binds "source" and reads "result".
		// The boundary pair already exists (every Graph is born with its interface) — the scene only
		// gives it pins.
		flow::GroupInputNode& inNode = graph.boundaryInputNode();
		flow::GroupOutputNode& outNode = graph.boundaryOutputNode();
		inNode.addBoundary<image::Image>("source");
		outNode.addBoundary<image::Image>("result");
		const flow::NodeId in = inNode.id();
		const flow::NodeId out = outNode.id();
		const flow::NodeId tint = graph.add(factory.create(kTintKey));
		const flow::NodeId blur = graph.add(factory.create(kBlurKey));

		graph.connect(in, 0, tint, 0);
		graph.connect(tint, 0, blur, 0);
		graph.connect(blur, 0, out, 0);
	}

	bool bindDefaultInput(const flow::BoundaryInput& pin, flow::Evaluation& evaluation, std::uint32_t size)
	{
		// The pin is checked, and this is not defensive tidiness: Evaluation::bind does NO type
		// checking, so binding a gradient onto a pin of another type installs a wrongly-typed value
		// that nothing complains about until a node reads it. This used to bind inputs[0] whatever
		// was asked for, which was harmless only while every graph had exactly one boundary input —
		// the moment a sweep adds a FramePosition pin, an unbound frame would have re-bound the
		// image.
		if (pin.type != typeid(image::Image))
			return false;

		// Generate a gradient image the way the old scene's source did, and bind it as the default
		// — so gui-mode has something to show until the user binds a file. The generator is run in a
		// throwaway graph of its own: a node's values live in an evaluation now, so producing one
		// outside a run means giving it one.
		flow::Graph scratch;
		const flow::NodeId id = scratch.add<flow::example::GradientNode>(size, size);
		flow::Evaluation scratchEval{scratch};
		flow::SerialScheduler{}.run(scratch, scratchEval);

		const flow::PortAddress produced{id, scratch.node(id).output(0).id()};
		if (scratchEval.value(produced).empty())
			return false;

		evaluation.bind(pin, scratchEval.value(produced));
		return true;
	}
} // namespace flowview
