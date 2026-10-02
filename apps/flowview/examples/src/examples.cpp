#include "examples.h"

#include "graphio.h" // saveGraph / sceneCodecs / templateResolver: the production save path

#include <lain/camera/board/pattern.h>
#include <lain/camera/flow/register.h>
#include <lain/core/uri.h>
#include <lain/data/value.h>
#include <lain/flow/dynamicports.h>
#include <lain/flow/edit.h>
#include <lain/flow/example/comparenode.h> // Comparison + the payload-type name a Compare retypes by
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/nodes/cast.h>
#include <lain/flow/nodes/constant.h>
#include <lain/flow/porttype.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/serialize/serialize.h>
#include <lain/image/color.h>
#include <lain/image/colorspace.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace flowview::examples
{
	using namespace lain;
	using flow::Graph;
	using flow::NodeId;
	using flow::Port;
	using flow::PortId;

	// The size every generated image shares: small enough to keep the data folder tiny, and 16:9 so a
	// thumbnail reads as footage. The stills and the shot are this size too, because Combine refuses
	// to average images of different extents.
	static constexpr int kWidth = 160;
	static constexpr int kHeight = 90;

	struct Context
	{
		const core::Factory<flow::Node>& factory;
		std::filesystem::path folder; // absolute; where the documents and data/ live

		std::filesystem::path data(const std::string& relative) const { return folder / "data" / relative; }
	};

	struct Document
	{
		Graph graph;
		flow::serialize::EditorTree layout;
		// A last edit to the SAVED document, for a broken example no graph can express: a kind the
		// factory does not know, or a template that is not there.
		std::function<void(data::Value&)> patch;
	};

	[[noreturn]] static void fail(const std::string& what)
	{
		throw std::runtime_error(what);
	}

	// One graph level being built, and its layout. Nodes are placed on a grid so a document opens
	// arranged; the gui's fallback gives every node a column of its own.
	class Level
	{
	public:
		Level(const Context& ctx, Graph& graph, flow::serialize::EditorTree& layout)
			: m_ctx(ctx)
			, m_graph(graph)
			, m_layout(layout)
		{
		}

		Graph& graph() { return m_graph; }

		NodeId add(const std::string& kind, int col, int row)
		{
			std::unique_ptr<flow::Node> node = m_ctx.factory.create(kind);
			if (!node)
				fail("no node kind \"" + kind + "\"");
			const NodeId id = m_graph.add(std::move(node));
			place(id, col, row);
			return id;
		}

		void place(NodeId id, int col, int row)
		{
			data::Value at = data::Value::object();
			at.set("x", data::Value(40.0 + 230.0 * col));
			at.set("y", data::Value(40.0 + 150.0 * row));
			m_layout.nodes[id] = std::move(at);
		}

		// The boundary pair every graph is born with, placed.
		NodeId input(int col, int row)
		{
			place(m_graph.boundaryInputNode().id(), col, row);
			return m_graph.boundaryInputNode().id();
		}
		NodeId output(int col, int row)
		{
			place(m_graph.boundaryOutputNode().id(), col, row);
			return m_graph.boundaryOutputNode().id();
		}

		template <typename T>
		void inputPin(const std::string& name)
		{
			if (m_graph.boundaryInputNode().addBoundary<T>(name) == PortId{})
				fail("could not add boundary input \"" + name + "\"");
		}
		template <typename T>
		void outputPin(const std::string& name)
		{
			if (m_graph.boundaryOutputNode().addBoundary<T>(name) == PortId{})
				fail("could not add boundary output \"" + name + "\"");
		}

		const Port& port(NodeId node, Port::Direction side, const std::string& name) const
		{
			const flow::Node& n = m_graph.node(node);
			const std::size_t count = side == Port::Direction::Input ? n.inputCount() : n.outputCount();
			for (std::size_t i = 0; i < count; ++i)
			{
				const Port& p = side == Port::Direction::Input ? n.input(i) : n.output(i);
				if (p.name() == name)
					return p;
			}
			fail(n.name() + " has no " + (side == Port::Direction::Input ? "input" : "output") + " \"" + name + "\"");
		}

		void wire(NodeId from, const std::string& out, NodeId to, const std::string& in)
		{
			const flow::PortAddress source{from, port(from, Port::Direction::Output, out).id()};
			const flow::PortAddress target{to, port(to, Port::Direction::Input, in).id()};
			if (m_graph.connect(source, target) != flow::Connection::Ok)
				fail("cannot connect " + m_graph.node(from).name() + "." + out + " -> " + m_graph.node(to).name() + "." + in);
		}

		// Set a param by name. A defaulted input's param carries the input's name.
		template <typename T>
		void set(NodeId node, const std::string& param, T value)
		{
			flow::Node& n = m_graph.node(node);
			for (std::size_t i = 0; i < n.paramCount(); ++i)
			{
				if (n.param(i).name() == param)
				{
					if (!n.setParam<T>(n.param(i).id(), std::move(value)))
						fail(n.name() + ": param \"" + param + "\" refused its value");
					return;
				}
			}
			fail(n.name() + " has no param \"" + param + "\"");
		}

		void retype(NodeId node, const std::string& payload, const flow::PortType& type)
		{
			if (!flow::edit::setPayloadType(m_graph, node, payload, &type).ok)
				fail(m_graph.node(node).name() + ": payload type \"" + payload + "\" refused");
		}

		// A dynamic pin on a Merge or a Select, by port-type key: the registry's own door.
		void branch(NodeId node, const std::string& typeKey, const std::string& name)
		{
			auto* dynamic = dynamic_cast<flow::DynamicPortsNode*>(&m_graph.node(node));
			if (!dynamic || flow::addPortOfType(*dynamic, typeKey, name) == PortId{})
				fail("could not add branch \"" + name + "\"");
		}

		// The interior of a group, map or loop on this level, as a Level of its own. Its layout is
		// that node's subtree of this level's.
		Level interior(NodeId group)
		{
			auto* owner = dynamic_cast<flow::GroupNode*>(&m_graph.node(group));
			Graph* inner = owner ? owner->editableInner() : nullptr;
			if (!inner)
				fail(m_graph.node(group).name() + " has no interior to build in");
			return Level{m_ctx, *inner, m_layout.groups[group]};
		}

		// Mirror a group's interior onto its face, as the host does every frame.
		flow::edit::GroupSync sync(NodeId group) { return flow::edit::syncGroupPorts(m_graph, group); }

		// A linked group on `source`, resolved through the loader's own routine, so it is built
		// exactly as a load would build it: the template has to be written already.
		NodeId linked(const std::string& source, int col, int row)
		{
			const NodeId id = add("linkedGroup", col, row);
			auto& link = static_cast<flow::LinkedGroupNode&>(m_graph.node(id));
			link.setSource(source);
			const flow::serialize::ResolveResult resolved = flow::serialize::resolveLinkedGroup(
				link, m_ctx.factory, sceneCodecs(), templateResolver(m_ctx.folder), nullptr);
			if (!resolved.clean() || !link.resolved())
				fail("template \"" + source + "\" did not resolve");
			sync(id);
			return id;
		}

		// The shared building blocks.
		NodeId gradient(int col, int row)
		{
			const NodeId id = add("gradient", col, row);
			set<int>(id, "width", kWidth);
			set<int>(id, "height", kHeight);
			return id;
		}

		template <typename T>
		NodeId constant(const T& value, int col, int row)
		{
			const NodeId id = add("constant", col, row);
			retype(id, flow::ConstantNode::kValuePayload, flow::portType<T>());
			set<T>(id, "value", value);
			return id;
		}

		NodeId cast(const flow::PortType& from, const flow::PortType& to, int col, int row)
		{
			const NodeId id = add("cast", col, row);
			retype(id, flow::CastNode::kFromPayload, from);
			retype(id, flow::CastNode::kToPayload, to);
			return id;
		}

	private:
		const Context& m_ctx;
		Graph& m_graph;
		flow::serialize::EditorTree& m_layout;
	};

	// A loop carrying one image through a blur, `count` times at most. Returns the loop; its
	// interior is built further by the caller when it needs a condition.
	static NodeId blurLoop(Level& at, int count, int col, int row)
	{
		const NodeId loop = at.add("loop", col, row);
		auto& node = static_cast<flow::LoopNode&>(at.graph().node(loop));
		if (node.addCarry<image::Image>("image").innerIn == PortId{})
			fail("could not add the loop's carry");
		Level inner = at.interior(loop);
		const NodeId in = inner.input(0, 0);
		const NodeId blur = inner.add("blur", 1, 0);
		const NodeId out = inner.output(3, 0);
		inner.wire(in, "image", blur, "image");
		inner.wire(blur, "image", out, "image");
		at.sync(loop);
		at.set<int>(loop, "count", count);
		return loop;
	}

	// The inner blur of a blurLoop.
	static NodeId loopBlur(Level& inner)
	{
		for (const NodeId id : inner.graph().nodeIds())
		{
			if (inner.graph().node(id).name() == "Blur")
				return id;
		}
		fail("the loop has no blur");
	}

	// --- the examples ---------------------------------------------------------

	static Document templateSoften(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.inputPin<image::Image>("image");
		root.outputPin<image::Image>("image");
		const NodeId in = root.input(0, 0);
		const NodeId tint = root.add("tint", 1, 0);
		root.set(tint, "tint", image::ColorRGBf{1.0f, 0.85f, 0.7f});
		const NodeId blur = root.add("blur", 2, 0);
		root.set<int>(blur, "radius", 3);
		root.set<float>(blur, "sigma", 2.0f);
		const NodeId out = root.output(3, 0);
		root.wire(in, "image", tint, "image");
		root.wire(tint, "image", blur, "image");
		root.wire(blur, "image", out, "image");
		return doc;
	}

	static Document tintBlur(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId tint = root.add("tint", 2, 0);
		const NodeId sigma = root.constant(3.0f, 2, 1);
		const NodeId blur = root.add("blur", 3, 0);
		const NodeId out = root.output(4, 0);
		root.wire(gradient, "image", tint, "image");
		root.wire(tint, "image", blur, "image");
		root.wire(sigma, "out", blur, "sigma"); // radius is left at its default
		root.wire(blur, "image", out, "result");
		return doc;
	}

	static Document payloadTypes(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId three = root.constant(3, 1, 1);
		const NodeId two = root.constant(std::string{"2"}, 1, 2);
		const NodeId toFloat = root.cast(flow::portType<int>(), flow::portType<float>(), 2, 1);
		const NodeId toInt = root.cast(flow::portType<std::string>(), flow::portType<int>(), 2, 2);
		const NodeId blur = root.add("blur", 3, 0);
		const NodeId compare = root.add("compare", 3, 2);
		root.retype(compare, flow::example::CompareNode::kValuePayload, flow::portType<int>());
		root.set<int>(compare, "b", 2);
		const NodeId gate = root.add("gate", 4, 0);
		const NodeId out = root.output(5, 0);
		root.wire(gradient, "image", blur, "image");
		root.wire(three, "out", toFloat, "in");
		root.wire(toFloat, "out", blur, "sigma");
		root.wire(two, "out", toInt, "in");
		root.wire(toInt, "out", blur, "radius");
		root.wire(three, "out", compare, "a"); // 3 > 2, so the gate is open
		root.wire(compare, "result", gate, "enable");
		root.wire(blur, "image", gate, "value");
		root.wire(gate, "out", out, "result");
		return doc;
	}

	static Document controlFlow(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("merged");
		root.outputPin<image::Image>("selected");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId tint = root.add("tint", 2, 0);
		const NodeId blur = root.add("blur", 2, 1);
		const NodeId off = root.constant(false, 2, 2);
		const NodeId gate = root.add("gate", 3, 0);
		const NodeId merge = root.add("merge", 4, 0);
		root.branch(merge, "Image", "gated");
		root.branch(merge, "Image", "blurred");
		const NodeId which = root.constant(1, 3, 2);
		const NodeId select = root.add("select", 4, 1);
		root.branch(select, "Image", "tinted");
		root.branch(select, "Image", "blurred");
		const NodeId out = root.output(5, 0);
		root.wire(gradient, "image", tint, "image");
		root.wire(gradient, "image", blur, "image");
		root.wire(off, "out", gate, "enable"); // closed: the gate suppresses, and everything after it dims
		root.wire(tint, "image", gate, "value");
		root.wire(gate, "out", merge, "gated"); // so Merge forwards its first LIVE branch, the blur
		root.wire(blur, "image", merge, "blurred");
		root.wire(which, "out", select, "selector"); // branch 1: the blur
		root.wire(tint, "image", select, "tinted");
		root.wire(blur, "image", select, "blurred");
		root.wire(merge, "out", out, "merged");
		root.wire(select, "out", out, "selected");
		return doc;
	}

	static Document colourConvert(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("bt709");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId convert = root.add("convert", 2, 0);
		root.set(convert, "assume", image::ColorSpace::sRGB);
		root.set(convert, "pixelFormat", image::PixelFormat::RGB8);
		root.set(convert, "colorSpace", image::ColorSpace::BT709);
		const NodeId out = root.output(3, 0);
		root.wire(gradient, "image", convert, "image");
		root.wire(convert, "image", out, "bt709");
		return doc;
	}

	static Document inlineGroup(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId soften = root.add("group", 2, 0);
		root.graph().node(soften).setName("soften");
		const NodeId out = root.output(3, 0);

		Level inner = root.interior(soften);
		inner.inputPin<image::Image>("image");
		inner.outputPin<image::Image>("image");
		const NodeId in = inner.input(0, 0);
		const NodeId tint = inner.add("tint", 1, 0);
		const NodeId twice = inner.add("group", 2, 0);
		inner.graph().node(twice).setName("blur twice");
		const NodeId innerOut = inner.output(3, 0);

		Level deepest = inner.interior(twice);
		deepest.inputPin<image::Image>("image");
		deepest.outputPin<image::Image>("image");
		const NodeId deepIn = deepest.input(0, 0);
		const NodeId first = deepest.add("blur", 1, 0);
		const NodeId second = deepest.add("blur", 2, 0);
		const NodeId deepOut = deepest.output(3, 0);
		deepest.wire(deepIn, "image", first, "image");
		deepest.wire(first, "image", second, "image");
		deepest.wire(second, "image", deepOut, "image");

		inner.sync(twice);
		inner.wire(in, "image", tint, "image");
		inner.wire(tint, "image", twice, "image");
		inner.wire(twice, "image", innerOut, "image");

		root.sync(soften);
		root.wire(gradient, "image", soften, "image");
		root.wire(soften, "image", out, "result");
		return doc;
	}

	static Document linkedGroup(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("a");
		root.outputPin<image::Image>("b");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId first = root.linked("template-soften.json", 2, 0);
		const NodeId blur = root.add("blur", 2, 1);
		const NodeId second = root.linked("template-soften.json", 3, 1);
		const NodeId out = root.output(4, 0);
		root.wire(gradient, "image", first, "image");
		root.wire(gradient, "image", blur, "image");
		root.wire(blur, "image", second, "image");
		root.wire(first, "image", out, "a");
		root.wire(second, "image", out, "b");
		return doc;
	}

	static Document countLoop(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.outputPin<int>("iterations");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId loop = blurLoop(root, 5, 2, 0);
		const NodeId out = root.output(3, 0);
		root.wire(gradient, "image", loop, "image");
		root.wire(loop, "image", out, "result");
		root.wire(loop, "iterations", out, "iterations");
		return doc;
	}

	static Document whileLoop(const Context& ctx)
	{
		// Blur until the picture stops changing: literally, since a clamped blur of an 8-bit image
		// reaches an exact fixed point, so the threshold is 0 and `count` is only the bound.
		Document doc = countLoop(ctx);
		Level root{ctx, doc.graph, doc.layout};
		NodeId loop;
		for (const NodeId id : doc.graph.nodeIds())
		{
			if (doc.graph.node(id).interiorEvaluation() == flow::InteriorEvaluation::PerIteration)
				loop = id;
		}
		root.set<int>(loop, "count", 100);
		Level inner = root.interior(loop);
		const NodeId in = inner.graph().boundaryInputNode().id();
		const NodeId out = inner.graph().boundaryOutputNode().id();
		const NodeId blur = loopBlur(inner);
		const NodeId difference = inner.add("imageDifference", 1, 1);
		const NodeId compare = inner.add("compare", 2, 1); // Float, Greater: keep going while it moved
		inner.set<float>(compare, "b", 0.0f);
		inner.wire(in, "image", difference, "a");
		inner.wire(blur, "image", difference, "b");
		inner.wire(difference, "difference", compare, "a");
		inner.wire(compare, "result", out, flow::LoopNode::kContinuePin);
		return doc;
	}

	static Document loopIndex(const Context& ctx)
	{
		// The loop's own index drives its condition and its body. `continue` is a post-test, so a
		// loop is a do-while: `index < 4` runs FIVE passes, the last at index 4.
		Document doc = countLoop(ctx);
		Level root{ctx, doc.graph, doc.layout};
		NodeId loop;
		for (const NodeId id : doc.graph.nodeIds())
		{
			if (doc.graph.node(id).interiorEvaluation() == flow::InteriorEvaluation::PerIteration)
				loop = id;
		}
		root.set<int>(loop, "count", 100);
		Level inner = root.interior(loop);
		const NodeId in = inner.graph().boundaryInputNode().id();
		const NodeId out = inner.graph().boundaryOutputNode().id();
		const NodeId blur = loopBlur(inner);
		const NodeId compare = inner.add("compare", 2, 1);
		inner.retype(compare, flow::example::CompareNode::kValuePayload, flow::portType<int>());
		inner.set<int>(compare, "b", 4);
		inner.set(compare, "op", flow::example::Comparison::Less);
		const NodeId toFloat = inner.cast(flow::portType<int>(), flow::portType<float>(), 0, 1);
		inner.wire(in, flow::LoopNode::kIndexPin, compare, "a");
		inner.wire(compare, "result", out, flow::LoopNode::kContinuePin);
		inner.wire(in, flow::LoopNode::kIndexPin, toFloat, "in");
		inner.wire(toFloat, "out", blur, "sigma"); // each pass blurs a little harder than the last
		return doc;
	}

	static Document loadImage(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		const NodeId load = root.add("loadimage", 1, 0);
		root.set(load, "path", ctx.data("stills/a.png"));
		const NodeId tint = root.add("tint", 2, 0);
		const NodeId out = root.output(3, 0);
		root.wire(load, "image", tint, "image");
		root.wire(tint, "image", out, "result");
		return doc;
	}

	// listDir(folder) -> map(file -> loadimage [-> blur]) -> combine. With `sigma`, the map also
	// takes a broadcast Float: one value to every element, where the files are split across them.
	static NodeId folderMap(Level& at, const std::filesystem::path& folder, bool sigma, NodeId& combine)
	{
		const NodeId list = at.add("listDir", 1, 0);
		at.set(list, "directory", folder);
		at.set(list, "extension", std::string{".png"});
		const NodeId map = at.add("map", 2, 0);
		combine = at.add("combine", 3, 0);

		Level inner = at.interior(map);
		inner.inputPin<std::filesystem::path>("file");
		if (sigma)
		{
			inner.inputPin<float>("sigma");
			// BROADCAST, which is declared by mirroring the pin un-lifted before the sync; sync
			// leaves an already-mirrored pin alone. A Float has no list form, so lifting it would be
			// refused anyway.
			auto& node = static_cast<flow::MapNode&>(at.graph().node(map));
			const Port& pin = inner.port(inner.graph().boundaryInputNode().id(), Port::Direction::Output, "sigma");
			if (node.exposeBroadcast(Port::Direction::Input, pin) == PortId{})
				fail("could not broadcast sigma");
		}
		inner.outputPin<image::Image>("image");
		const NodeId in = inner.input(0, 0);
		const NodeId load = inner.add("loadimage", 1, 0);
		NodeId last = load;
		inner.wire(in, "file", load, "path");
		if (sigma)
		{
			const NodeId blur = inner.add("blur", 2, 0);
			inner.wire(load, "image", blur, "image");
			inner.wire(in, "sigma", blur, "sigma");
			last = blur;
		}
		inner.wire(last, "image", inner.output(3, 0), "image");

		at.sync(map);
		at.wire(list, "files", map, "file");
		at.wire(map, "image", combine, "images");
		return map;
	}

	static Document folderAverage(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		NodeId combine;
		const NodeId map = folderMap(root, ctx.data("stills"), true, combine);
		const NodeId sigma = root.constant(1.5f, 1, 1);
		const NodeId out = root.output(4, 0);
		root.wire(sigma, "out", map, "sigma");
		root.wire(combine, "image", out, "result");
		return doc;
	}

	static std::filesystem::path shotPattern(const Context& ctx)
	{
		return ctx.data("shot/shot.<frame:04>.png");
	}

	static Document sequenceClip(const Context& ctx)
	{
		// Clip 12 frames from position 6. Position re-bases, identity does not: the clip's frame 0 is
		// the shot's seventh frame, and its burned-in number says so.
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<media::FrameSequence>("clip");
		root.outputPin<image::Image>("first");
		root.input(0, 0);
		const NodeId open = root.add("openSequence", 1, 0);
		root.set(open, "path", shotPattern(ctx));
		const NodeId clip = root.add("clipSequence", 2, 0);
		root.set(clip, "position", media::FramePosition{6});
		root.set<int>(clip, "count", 12);
		const NodeId frame = root.add("frameAt", 3, 1);
		const NodeId out = root.output(4, 0);
		root.wire(open, "sequence", clip, "sequence");
		root.wire(clip, "clipped", frame, "sequence");
		root.wire(clip, "clipped", out, "clip");
		root.wire(frame, "image", out, "first");
		return doc;
	}

	static Document sequenceRender(const Context& ctx)
	{
		// The one example with a root input: the cli sweep finds its frame counter BY TYPE, and a
		// bound value is not saved, so in the gui this opens empty until Position is dragged.
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.inputPin<media::FramePosition>("position");
		root.outputPin<image::Image>("result");
		const NodeId in = root.input(0, 1);
		const NodeId open = root.add("openSequence", 1, 0);
		root.set(open, "path", shotPattern(ctx));
		const NodeId frame = root.add("frameAt", 2, 0);
		const NodeId tint = root.add("tint", 3, 0);
		const NodeId out = root.output(4, 0);
		root.wire(open, "sequence", frame, "sequence");
		root.wire(in, "position", frame, "position");
		root.wire(frame, "image", tint, "image");
		root.wire(tint, "image", out, "result");
		return doc;
	}

	static Document video(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<media::FrameSequence>("sequence");
		root.outputPin<image::Image>("frame");
		root.input(0, 0);
		const NodeId open = root.add("openSequence", 1, 0);
		root.set(open, "path", ctx.data("shot.mp4"));
		const NodeId frame = root.add("frameAt", 2, 1);
		root.set(frame, "position", media::FramePosition{12});
		const NodeId out = root.output(3, 0);
		root.wire(open, "sequence", frame, "sequence");
		root.wire(open, "sequence", out, "sequence");
		root.wire(frame, "image", out, "frame");
		return doc;
	}

	static Document bagel(const Context& ctx)
	{
		// Nesting across every kind: an inline group holding a map, whose interior runs each still
		// through a LINKED template and then a LOOP. What it exercises is the breadcrumb, the element
		// stepper and the freshness roll-up across levels, not any one node.
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId pipeline = root.add("group", 1, 1);
		root.graph().node(pipeline).setName("pipeline");
		const NodeId which = root.constant(1, 2, 2);
		const NodeId select = root.add("select", 3, 0);
		root.branch(select, "Image", "plain");
		root.branch(select, "Image", "processed");
		const NodeId out = root.output(4, 0);

		Level group = root.interior(pipeline);
		group.outputPin<image::Image>("image");
		group.input(0, 0);
		NodeId combine;
		const NodeId map = folderMap(group, ctx.data("stills"), false, combine);
		group.wire(combine, "image", group.output(4, 0), "image");

		Level element = group.interior(map);
		const NodeId load = [&]
		{
			for (const NodeId id : element.graph().nodeIds())
			{
				if (element.graph().node(id).name() == "LoadImage")
					return id;
			}
			fail("the map has no LoadImage");
		}();
		const NodeId elementOut = element.graph().boundaryOutputNode().id();
		element.graph().disconnect(flow::PortAddress{elementOut, element.port(elementOut, Port::Direction::Input, "image").id()});
		const NodeId soften = element.linked("template-soften.json", 2, 0);
		const NodeId loop = blurLoop(element, 3, 3, 0);
		element.place(elementOut, 4, 0);
		element.wire(load, "image", soften, "image");
		element.wire(soften, "image", loop, "image");
		element.wire(loop, "image", elementOut, "image");

		root.sync(pipeline);
		root.wire(which, "out", select, "selector");
		root.wire(gradient, "image", select, "plain");
		root.wire(pipeline, "image", select, "processed");
		root.wire(select, "out", out, "result");
		return doc;
	}

	// --- broken on purpose ----------------------------------------------------

	// Every node object in a saved body, nested bodies included.
	static void forEachNode(data::Value& body, const std::function<void(data::Value&)>& visit)
	{
		data::Value::Object* entries = body.asObject();
		if (!entries)
			return;
		for (auto& [key, value] : *entries)
		{
			if (key != "nodes" || !value.asArray())
				continue;
			for (data::Value& node : *value.asArray())
			{
				visit(node);
				if (data::Value::Object* fields = node.asObject())
				{
					for (auto& [field, inner] : *fields)
					{
						if (field == "graph")
							forEachNode(inner, visit);
					}
				}
			}
		}
	}

	static bool isKind(const data::Value& node, const std::string& kind)
	{
		const data::Value* stored = node.find("kind");
		return stored && stored->asString() && *stored->asString() == kind;
	}

	static Document brokenMissingTemplate(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId link = root.linked("template-soften.json", 2, 0);
		const NodeId out = root.output(3, 0);
		root.wire(gradient, "image", link, "image");
		root.wire(link, "image", out, "result");
		// Point it at a file that is not there. Its interface cache still rebuilds the face, so the
		// wiring survives and a save is lossless: a broken link is repairable, not destructive.
		doc.patch = [](data::Value& saved)
		{
			forEachNode(saved, [](data::Value& node)
						{
							if (isKind(node, "linkedGroup"))
								node.set("source", data::Value(std::string{"template-missing.json"})); });
		};
		return doc;
	}

	static Document brokenMapHole(const Context& ctx)
	{
		// folder-average over a folder holding one file that is not a PNG. The BLUR is what makes the
		// hole: a failed load emits an invalid image rather than nothing, and it is the blur that
		// turns an invalid image into no value, which the gather sees as a hole.
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("result");
		root.input(0, 0);
		NodeId combine;
		const NodeId map = folderMap(root, ctx.data("holes"), true, combine);
		const NodeId sigma = root.constant(1.5f, 1, 1);
		root.wire(sigma, "out", map, "sigma");
		root.wire(combine, "image", root.output(4, 0), "result");
		return doc;
	}

	static Document brokenCastPair(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<int>("number");
		root.input(0, 0);
		const NodeId gradient = root.gradient(1, 0);
		const NodeId cast = root.cast(flow::portType<image::Image>(), flow::portType<int>(), 2, 0);
		const NodeId out = root.output(3, 0);
		root.wire(gradient, "image", cast, "in");
		root.wire(cast, "out", out, "number");
		return doc;
	}

	static Document brokenLoopPin(const Context& ctx)
	{
		Document doc = countLoop(ctx);
		Level root{ctx, doc.graph, doc.layout};
		for (const NodeId id : doc.graph.nodeIds())
		{
			if (doc.graph.node(id).interiorEvaluation() != flow::InteriorEvaluation::PerIteration)
				continue;
			// An inner pin named like the loop's own `count` port cannot be mirrored onto its face.
			Level inner = root.interior(id);
			inner.inputPin<int>("count");
			if (root.sync(id).refused.empty())
				fail("the colliding pin was not refused");
		}
		return doc;
	}

	static Document brokenUnknownKind(const Context& ctx)
	{
		Document doc = tintBlur(ctx);
		// A kind this build does not know: the node and its edges are dropped on load, and said so.
		doc.patch = [](data::Value& saved)
		{
			forEachNode(saved, [](data::Value& node)
						{
							if (isKind(node, "blur"))
								node.set("kind", data::Value(std::string{"sharpen"})); });
		};
		return doc;
	}

	// A ChArUco board to print for a real-camera fixture: 9 x 6 squares of 30 mm, markers three
	// quarters of a square, from ARUCO 5x5_100. 354 pixels per square is 30 mm at 300 dpi, with a 5 mm
	// quiet zone, which fits A4 landscape inside a printer's margins. Whatever scale it prints at, the
	// square is MEASURED afterwards and the measurement is what a fixture records.
	static Document renderBoard(const Context& ctx)
	{
		Document doc;
		Level root{ctx, doc.graph, doc.layout};
		root.outputPin<image::Image>("board");
		root.outputPin<std::string>("description");
		root.input(0, 0);
		const NodeId spec = root.add(camera::kBoardSpecificationKey, 1, 0);
		root.set(spec, "dictionary", camera::board::Dictionary::Aruco5x5_100);
		root.set<int>(spec, "squaresX", 9);
		root.set<int>(spec, "squaresY", 6);
		root.set<float>(spec, "markerToSquare", 0.75f);
		root.set<std::string>(spec, "identity", "lain-9x6-30mm");
		root.set<float>(spec, "squareLengthMm", 30.0f);
		const NodeId render = root.add(camera::kRenderBoardKey, 2, 0);
		root.set<int>(render, "pixelsPerSquare", 354);
		root.set<int>(render, "marginPixels", 59);
		const NodeId out = root.output(3, 0);
		root.wire(spec, "board", render, "board");
		root.wire(render, "image", out, "board");
		root.wire(render, "description", out, "description");
		return doc;
	}

	const std::vector<Example>& catalog()
	{
		static const std::vector<Example> examples = {
			{"template-soften", "a linked-group template: tint, then blur (opens with its input unbound)", false, "", &templateSoften},
			{"tint-blur", "gradient -> tint -> blur, with sigma driven by a Float constant", false, "", &tintBlur},
			{"payload-types", "retyped Constants, Casts into a blur's settings, a Compare over Int opening a Gate", false, "", &payloadTypes},
			{"control-flow", "a closed Gate suppressing a branch, Merge taking the first live one, Select choosing", false, "", &controlFlow},
			{"colour-convert", "gradient -> convert to BT709 RGB8", false, "", &colourConvert},
			{"inline-group", "an inline group holding another: a two-deep breadcrumb", false, "", &inlineGroup},
			{"linked-group", "two linked instances of template-soften, sharing one definition", false, "", &linkedGroup},
			{"count-loop", "a loop folding an image through a blur five times", false, "", &countLoop},
			{"while-loop", "blur until the picture stops changing; `iterations` says when", false, "", &whileLoop},
			{"loop-index", "the loop's index drives its condition and its body (index < 4 runs five passes)", false, "", &loopIndex},
			{"load-image", "loadimage data/stills/a.png -> tint", false, "", &loadImage},
			{"folder-average", "listDir -> map (split files, broadcast sigma) -> combine", false, "", &folderAverage},
			{"sequence-clip", "a 12-frame clip from position 6; its first frame shows the shot's 7th", false, "", &sequenceClip},
			{"sequence-render", "a frame per position; sweep it with `run --frame 0-23`", false, "", &sequenceRender},
			{"video", "data/shot.mp4 as a sequence, and its frame at position 12", true, "", &video},
			{"bagel", "an inline group holding a map, whose elements run a linked template and a loop", false, "", &bagel},
			{"render-board", "a ChArUco board to print for a real-camera fixture: 9 x 6 squares of 30 mm", false, "", &renderBoard, true},
			{"broken-missing-template", "a link to a template that is not there", false, "could not be resolved", &brokenMissingTemplate},
			{"broken-map-hole", "folder-average over a folder holding one unreadable file", false, "elements produced no", &brokenMapHole},
			{"broken-cast-pair", "a Cast from Image to Int, a pair nothing converts", false, "no conversion from", &brokenCastPair},
			{"broken-loop-pin", "a loop interior pin named like the loop's own `count`", false, "could not be mirrored", &brokenLoopPin},
			{"broken-unknown-kind", "tint-blur with its blur's kind unknown to this build", false, "unknown node kind", &brokenUnknownKind},
			{"broken-v1", "a version-1 document, migrated on load (hand-written, frozen)", false, "version 1", nullptr},
		};
		return examples;
	}

	// --- stable ids ------------------------------------------------------------

	static bool isUuidText(const std::string& text)
	{
		if (text.size() != 36)
			return false;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			const char c = text[i];
			const bool hyphen = i == 8 || i == 13 || i == 18 || i == 23;
			if (hyphen != (c == '-'))
				return false;
			if (!hyphen && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		}
		return true;
	}

	static void collectIds(const data::Value& value, std::map<std::string, std::string>& table)
	{
		const auto note = [&table](const std::string& id)
		{
			if (isUuidText(id) && table.count(id) == 0)
			{
				char text[37];
				std::snprintf(text, sizeof(text), "00000000-0000-7000-8000-%012zx", table.size() + 1);
				table.emplace(id, text);
			}
		};
		if (const std::string* text = value.asString())
			note(*text);
		else if (const data::Value::Array* items = value.asArray())
		{
			for (const data::Value& item : *items)
				collectIds(item, table);
		}
		else if (const data::Value::Object* entries = value.asObject())
		{
			for (const auto& [key, item] : *entries)
			{
				note(key);
				collectIds(item, table);
			}
		}
	}

	static data::Value renumbered(const data::Value& value, const std::map<std::string, std::string>& table)
	{
		const auto swap = [&table](const std::string& text)
		{
			const auto it = table.find(text);
			return it == table.end() ? text : it->second;
		};
		if (const std::string* text = value.asString())
			return data::Value(swap(*text));
		if (const data::Value::Array* items = value.asArray())
		{
			data::Value out = data::Value::array();
			for (const data::Value& item : *items)
				out.push(renumbered(item, table));
			return out;
		}
		if (const data::Value::Object* entries = value.asObject())
		{
			data::Value out = data::Value::object();
			for (const auto& [key, item] : *entries)
				out.set(swap(key), renumbered(item, table));
			return out;
		}
		return value;
	}

	// Minted ids are v7 uuids, fresh every run, so a regenerated document would differ from the last
	// in every id. Renumbered from a counter in the order they first appear, an unchanged example is
	// written byte for byte as it was. Ids need only be unique within a document, and each is.
	static data::Value stableIds(const data::Value& document)
	{
		std::map<std::string, std::string> table;
		collectIds(document, table);
		return renumbered(document, table);
	}

	void writeDocuments(const std::filesystem::path& folder, const core::Factory<flow::Node>& factory)
	{
		// Every build writes the same bytes, camera documents included: the camera kinds are in the
		// palette whatever this build's backends (registerCameraNodes registers them all).
		const Context ctx{factory, std::filesystem::absolute(folder).lexically_normal()};
		for (const Example& example : catalog())
		{
			if (!example.build)
				continue;
			try
			{
				Document doc = example.build(ctx);
				const core::Uri file = core::Uri::fromPath(ctx.folder / (example.name + ".json"));
				// Written through the production save first, so the document is exactly what the app
				// writes (paths made relative included), then renumbered and patched.
				if (!saveGraph(file, doc.graph, factory, doc.layout))
					fail("could not be written");
				std::optional<data::Value> saved = io::data::load(file);
				if (!saved)
					fail("could not be read back");
				if (doc.patch)
					doc.patch(*saved);
				if (!io::data::save(file, stableIds(*saved)))
					fail("could not be rewritten");
			}
			catch (const std::exception& error)
			{
				throw std::runtime_error(example.name + ": " + error.what());
			}
		}
	}
} // namespace flowview::examples
