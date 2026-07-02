#include "inspectorwindow.h"

#include "flowviewapp.h"

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/enums.h>
#include <lain/gui/gui.h>
#include <lain/gui/nodes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flowview
{
	using namespace lain;

	// A pin's editor id, in imnodes' attribute id-space (separate from node + link ids).
	// node * 1000 + (output ? 500 : 0) + port keeps inputs and outputs distinct; assumes
	// fewer than 500 ports per direction and node ids well under ~2M — fine for prototyping.
	static int pinId(flow::NodeId node, bool output, flow::PortIndex port)
	{
		return static_cast<int>(node.value()) * 1000 + (output ? 500 : 0) + static_cast<int>(port);
	}

	// Inverse of pinId(): decode an imnodes attribute id back to the (node, direction,
	// port) it stands for — used when a dragged link reports its endpoint pins.
	struct DecodedPin
	{
		flow::NodeId node;
		bool output;
		flow::PortIndex port;
	};

	static DecodedPin decodePin(int attr)
	{
		const int rem = attr % 1000;
		const bool output = rem >= 500;
		return DecodedPin{
			flow::NodeId{static_cast<std::uint64_t>(attr / 1000)},
			output,
			static_cast<flow::PortIndex>(output ? rem - 500 : rem)};
	}

	// A link was dragged between two pins: wire the output into the input. connect()
	// rejects a bad drag (same-direction pins, type mismatch, cycle, or an input
	// already in use), so a rejected drag simply doesn't stick. Returns whether an
	// edge was added.
	static bool tryConnect(flow::Graph& graph, int startAttr, int endAttr)
	{
		const DecodedPin a = decodePin(startAttr);
		const DecodedPin b = decodePin(endAttr);
		if (a.output == b.output)
			return false; // need exactly one output and one input
		const DecodedPin& out = a.output ? a : b;
		const DecodedPin& in = a.output ? b : a;
		return graph.connect(out.node, out.port, in.node, in.port) == flow::Connection::Ok;
	}

	// Disconnect every currently-selected link (Delete key). imnodes link ids are edge
	// indices; resolve them to edges *before* mutating, since disconnecting shifts the
	// indices. Returns whether any edge was removed.
	static bool deleteSelectedLinks(flow::Graph& graph)
	{
		const int selected = gui::nodes::NumSelectedLinks();
		if (selected <= 0)
			return false;

		std::vector<int> linkIds(static_cast<std::size_t>(selected));
		gui::nodes::GetSelectedLinks(linkIds.data());

		const std::vector<flow::Graph::Edge>& edges = graph.edges();
		std::vector<flow::Graph::Edge> toRemove;
		for (const int id : linkIds)
		{
			if (id >= 0 && static_cast<std::size_t>(id) < edges.size())
				toRemove.push_back(edges[static_cast<std::size_t>(id)]);
		}

		bool removed = false;
		for (const flow::Graph::Edge& e : toRemove)
			removed |= graph.disconnect(e.to, e.inPort);
		return removed;
	}

	// Thumbnail side length (pixels) for each preview size.
	static float previewExtent(PreviewSize size)
	{
		switch (size)
		{
			case PreviewSize::Small:
				return 96.0f;
			case PreviewSize::Medium:
				return 192.0f;
			case PreviewSize::Large:
				return 320.0f;
		}
		return 192.0f;
	}

	// A port's value as inspector text — CPU scalars/strings only. Textures are shown
	// as a thumbnail by the caller, not here, so this never touches the GPU (no
	// readback, no layout change every frame).
	static std::string cpuLabel(const flow::Port& port)
	{
		const flow::PortValue& value = port.value();
		if (value.holds<int>())
			return std::to_string(value.get<int>());
		if (value.holds<float>())
			return std::to_string(value.get<float>());
		if (value.holds<double>())
			return std::to_string(value.get<double>());
		if (value.holds<bool>())
			return value.get<bool>() ? "true" : "false";
		if (value.holds<std::string>())
			return value.get<std::string>();
		return std::string("<") + value.type().name() + '>';
	}

	bool InspectorWindow::onInit(app::Window& window)
	{
		m_guiCtx = std::make_unique<gui::Context>(window.app(), window);
		m_sampler = window.app().device().createSampler();
		return true;
	}

	void InspectorWindow::onRender(app::Window& window, const app::TimeState&)
	{
		FlowviewApp& appDelegate = window.app().getDelegate<FlowviewApp>();
		flow::Graph& graph = appDelegate.graph(); // mutated by the canvas below

		m_guiCtx->newFrame();

		gui::SetNextWindowPos(math::Vec2f{20.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 320.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Inspector");
		gui::enumCombo("Preview size", m_previewSize); // labels from lain::meta::enums
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			gui::Text("[%llu] %s", static_cast<unsigned long long>(id.value()), node.name().c_str());

			auto port = [&](const char* tag, const flow::Port& p)
			{
				if (!p.ready())
				{
					gui::Text("    %s %s: (empty)", tag, p.name().c_str());
					return;
				}
				if (p.type() == typeid(acm::Texture))
				{
					const acm::Texture& texture = p.value().get<acm::Texture>();
					const acm::Extent2D extent = texture.getExtent();
					gui::Text("    %s %s: %s %ux%u", tag, p.name().c_str(), std::string(p.typeName()).c_str(), extent.width, extent.height);
					if (!m_havePreview && texture.valid())
					{
						m_preview = m_guiCtx->image(texture, m_sampler);
						m_havePreview = true;
					}
					if (m_havePreview)
					{
						const float side = previewExtent(m_previewSize);
						gui::Image(m_preview, math::Vec2f{side, side});
					}
				}
				else
				{
					gui::Text("    %s %s: %s", tag, p.name().c_str(), cpuLabel(p).c_str());
				}
			};

			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				port("in ", node.input(i));
			for (flow::PortIndex i = 0; i < node.outputCount(); ++i)
				port("out", node.output(i));
		}
		gui::End();

		// The node canvas: the graph drawn as imnodes nodes + links (read-only). Node
		// ids are flow NodeIds; pins use pinId(); links use the edge index.
		gui::SetNextWindowPos(math::Vec2f{360.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{880.0f, 600.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Graph");
		gui::nodes::BeginNodeEditor();
		int column = 0;
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			if (!m_laidOut)
				gui::nodes::SetNodeGridSpacePos(static_cast<int>(id.value()), math::Vec2f{column * 220.0f, 40.0f + (column % 4) * 140.0f});

			gui::nodes::BeginNode(static_cast<int>(id.value()));
			gui::nodes::BeginNodeTitleBar();
			gui::Text("[%llu] %s", static_cast<unsigned long long>(id.value()), node.name().c_str());
			gui::nodes::EndNodeTitleBar();

			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& in = node.input(i);
				gui::nodes::BeginInputAttribute(pinId(id, false, i));
				gui::Text("%s : %s", in.name().c_str(), std::string(in.typeName()).c_str());
				gui::nodes::EndInputAttribute();
			}
			for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
			{
				const flow::Port& out = node.output(o);
				gui::nodes::BeginOutputAttribute(pinId(id, true, o));
				gui::Text("%s : %s", out.name().c_str(), std::string(out.typeName()).c_str());
				gui::nodes::EndOutputAttribute();
			}
			gui::nodes::EndNode();
			++column;
		}

		const std::vector<flow::Graph::Edge>& edges = graph.edges();
		for (std::size_t e = 0; e < edges.size(); ++e)
			gui::nodes::Link(static_cast<int>(e), pinId(edges[e].from, true, edges[e].outPort), pinId(edges[e].to, false, edges[e].inPort));

		gui::nodes::EndNodeEditor();

		// Canvas editing (must query imnodes after EndNodeEditor): a dragged link
		// connects its output->input; Delete removes the selected link(s).
		bool edited = false;
		int startAttr = 0;
		int endAttr = 0;
		if (gui::nodes::IsLinkCreated(&startAttr, &endAttr))
			edited |= tryConnect(graph, startAttr, endAttr);
		if (gui::IsWindowFocused() && gui::IsKeyPressed(ImGuiKey_Delete))
			edited |= deleteSelectedLinks(graph);

		gui::End();
		m_laidOut = true;

		// A topology edit changes what should flow; re-run the scene before rendering.
		if (edited)
			appDelegate.reevaluate();

		window.renderer().render([&](acm::CommandBuffer cmd, uint32_t)
								 { m_guiCtx->render(cmd); });
	}

	void InspectorWindow::onShutdown(app::Window&)
	{
		m_guiCtx.reset(); // destroy the ImGui backends before the device tears down
		m_sampler = {};	  // drop the sampler handle
		m_havePreview = false;
	}
} // namespace flowview
