#include "scene.h"

#include <lain/flow/boundary.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/example/blurnode.h>
#include <lain/flow/example/clipsequencenode.h>
#include <lain/flow/example/combinenode.h>
#include <lain/flow/example/comparenode.h>
#include <lain/flow/example/convertnode.h>
#include <lain/flow/example/frameatnode.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/example/imagedifferencenode.h>
#include <lain/flow/example/listdirnode.h>
#include <lain/flow/example/loadimagenode.h>
#include <lain/flow/example/opensequencenode.h>
#include <lain/flow/example/tintnode.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/nodes/cast.h>	  // CastNode — the conversion the graph authors
#include <lain/flow/nodes/constant.h> // ConstantNode — the payload-typed source
#include <lain/flow/nodes/gate.h>	  // control nodes over the scene payload (image::Image)
#include <lain/flow/nodes/merge.h>
#include <lain/flow/nodes/select.h>
#include <lain/flow/porttype.h> // portType<T> — a payload type's flyweight
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

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
	// ONE Constant kind for every payload type (ADR-0022). It replaced constInt / constBool /
	// constFloat / constPath / constString, which were five keys for one class template and grew
	// with every port type the app registered — the type a Constant emits is now a payload type the
	// document carries, chosen from the Inspector.
	static constexpr const char* kConstantKey = "constant";
	// The node the payload-type mechanism exists FOR (M12): a per-type Cast would need one key per
	// PAIR, which is not viable at any number of types.
	static constexpr const char* kCastKey = "cast";
	static constexpr const char* kGroupInputKey = "groupInput";
	static constexpr const char* kGroupOutputKey = "groupOutput";
	static constexpr const char* kListDirKey = "listDir";
	static constexpr const char* kCombineKey = "combine";
	static constexpr const char* kConvertKey = "convert";
	// The two halves of a loop's CONDITION (M11): measure, then decide. They exist so the while
	// vertical is real — without them the `continue` pin would be built and never driven.
	static constexpr const char* kImageDifferenceKey = "imageDifference";
	static constexpr const char* kCompareKey = "compare";

	// The frame-sequence kinds (M10). In the catalog since slice 7 — until then they were registered
	// in the factory alone and reachable only from the cli, the same order M8 used when the map node
	// ran headless a slice before it appeared in a menu.
	static constexpr const char* kOpenSequenceKey = "openSequence";
	static constexpr const char* kFrameAtKey = "frameAt";
	static constexpr const char* kClipSequenceKey = "clipSequence";
	static constexpr const char* kGroupKey = "group";
	static constexpr const char* kMapKey = "map";
	static constexpr const char* kLoopKey = "loop";
	static constexpr const char* kLinkedGroupKey = "linkedGroup";

	const std::vector<NodeCategory>& nodeCatalog()
	{
		// The palette-addable kinds, grouped by role (mirrors the canvasstyle title colours). Keys must
		// match the registrations below. Boundary nodes (GroupInput/GroupOutput) are intentionally NOT
		// here: they're a one-each-per-graph fixture that comes with a New graph and is grown from the
		// Interface panel, not added like an ordinary node.
		static const std::vector<NodeCategory> catalog = {
			{"Sources", {kGradientKey, kLoadImageKey, kListDirKey, kConstantKey}},
			{"Filters", {kTintKey, kBlurKey, kCombineKey, kConvertKey, kImageDifferenceKey}},
			// Footage. openSequence brings a folder of stills or a video file in as one sequence
			// (medium-neutral — the opener seam decides which), frameAt is where it becomes pixels a
			// graph can process, and clipSequence trims the range. A category of their own rather
			// than a source + two filters: what they have in common is the payload they pass, which
			// is what a user is looking for when reaching for them.
			{"Sequence", {kOpenSequenceKey, kFrameAtKey, kClipSequenceKey}},
			// Compare sits with the control nodes rather than the filters: what it produces is a
			// bool nothing looks at for its own sake — it gates, or it drives a loop's `continue`.
			// The graph's PLUMBING — what routes, tests and converts values, as opposed to what
			// processes pictures. Gate/Merge/Select route, Compare tests, Cast converts; none of them
			// produces a value anyone looks at for its own sake.
			{"Control", {kGateKey, kMergeKey, kSelectKey, kCompareKey, kCastKey}},
			// A group is added empty (its inner graph is born with its own boundary pair) and grown by
			// descending into it, and a MAP the same way — the difference is only that a map's face is
			// lifted, so its `files` input takes the whole collection its interior is written against
			// one element of. A LOOP is added empty too and is likewise grown from inside, but what
			// makes it a loop is authored rather than derived: its CARRIES, paired in the Interface
			// pane. A LINKED group needs a template chosen first, so the menu bar adds it through a
			// file dialog rather than from this list.
			{"Groups", {kGroupKey, kMapKey, kLoopKey}},
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
		// The condition path of a WHILE loop (M11): measure the change, then decide. Registered
		// beside the other example nodes because that is all they are — a loop needs no engine
		// support for its condition beyond the reserved pin itself.
		factory.registerType<flow::example::ImageDifferenceNode>(kImageDifferenceKey);
		factory.registerType<flow::example::CompareNode>(kCompareKey);
		factory.registerType<flow::example::OpenSequenceNode>(kOpenSequenceKey);
		factory.registerType<flow::example::FrameAtNode>(kFrameAtKey);
		factory.registerType<flow::example::ClipSequenceNode>(kClipSequenceKey);

		// Control nodes, PRESET to the scene payload. Gate passes its value when enabled (else
		// suppresses downstream); the variadic Merge/Select are empty at construction — the canvas ±
		// grows their branches, filtered to whatever payload type the node currently carries.
		//
		// Image is the preset because it is what these were fixed to before M12, so every document
		// written until now — which names no `types` section for them — loads exactly as it did.
		// What is new is that a graph can now gate a float, merge bools or select between paths.
		factory.registerType<flow::GateNode>(kGateKey, std::cref(flow::portType<image::Image>()));
		factory.registerType<flow::MergeNode>(kMergeKey, std::cref(flow::portType<image::Image>()));
		factory.registerType<flow::SelectNode>(kSelectKey, std::cref(flow::portType<image::Image>()));

		// The one Constant. It drives whatever a graph needs a fixed value for — a Gate's bool
		// `enable`, a Select's int `selector`, a Blur's float `sigma`, a ListDir's folder or
		// extension filter — and which of those it is, is the payload type on the node rather than
		// which of five keys was used to make it.
		//
		// Wiring one is how a SETTING becomes part of the graph (see flow::Default): drivable from a
		// boundary input and visible on the canvas, instead of buried in a node's inspector.
		//
		// The preset is Int. A fresh Constant has to be something, and the Inspector's payload-type
		// dropdown is one click away; an old document that names a type carries it in `types`.
		factory.registerType<flow::ConstantNode>(kConstantKey, std::cref(flow::portType<int>()));

		// The five keys `constant` replaced, kept so documents written before it still open. They go
		// in through the STRING-CREATOR form, which records no reverse type -> key entry: a Factory
		// maps one class to exactly one key, so registering these the typed way would make keyOf
		// answer whichever registered last, and a Float constant would SAVE as "constInt" carrying a
		// `types` section that contradicts its own kind. Nothing ever saves under them; they are
		// deletable whole once no document in the wild names one.
		const auto legacyConstant = [](const flow::PortType& type)
		{ return [&type]() -> std::unique_ptr<flow::Node>
		  { return std::make_unique<flow::ConstantNode>(type); }; };
		factory.registerType("constInt", legacyConstant(flow::portType<int>()));
		factory.registerType("constBool", legacyConstant(flow::portType<bool>()));
		factory.registerType("constFloat", legacyConstant(flow::portType<float>()));
		factory.registerType("constPath", legacyConstant(flow::portType<std::filesystem::path>()));
		factory.registerType("constString", legacyConstant(flow::portType<std::string>()));

		// The Cast, preset Int -> Float: the pair that unblocked driving a float setting from a
		// loop's int `index`, and the one a fresh Cast is most often wanted for. Both ends are
		// payload types, changed from the Inspector.
		factory.registerType<flow::CastNode>(kCastKey, std::cref(flow::portType<int>()),
											 std::cref(flow::portType<float>()));

		// The boundary nodes are added to the scene directly (not via the palette), but they must be
		// in the factory too so serialization can name them (keyOf) and recreate them on load.
		factory.registerType<flow::GroupInputNode>(kGroupInputKey);
		factory.registerType<flow::GroupOutputNode>(kGroupOutputKey);

		// Group nodes. Both must be in the factory for serialization to name them; only the inline one
		// is palette-addable (a linked group is created by picking its template).
		factory.registerType<flow::InlineGroupNode>(kGroupKey);
		factory.registerType<flow::LinkedGroupNode>(kLinkedGroupKey);
		factory.registerType<flow::MapNode>(kMapKey);
		// The LOOP. Until this line nothing in the tree constructed one: slices 1-5 built it, ran it
		// and persisted it, and it was reachable from no menu and no document — which is the
		// compiled-linked-unreachable shape this repo has caught four times.
		factory.registerType<flow::LoopNode>(kLoopKey);
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
