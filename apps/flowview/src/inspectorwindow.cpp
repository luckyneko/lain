#include "inspectorwindow.h"

#include "flowviewapp.h"

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/enums.h>
#include <lain/gui/gui.h>
#include <lain/gui/nodes.h>

#include <cstddef>
#include <cstdint>
#include <set>
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

	// A link was dragged between two pins: orient them (one output, one input) and ask
	// the edit layer to wire it, replacing whatever fed the input. flow::edit rejects a
	// bad drag (type mismatch, cycle) and, on rejection, leaves the existing edge intact
	// — so a bad drag onto an occupied input no longer destroys its wire. Returns
	// whether an edge was made.
	static bool tryConnect(flow::Graph& graph, int startAttr, int endAttr)
	{
		const DecodedPin a = decodePin(startAttr);
		const DecodedPin b = decodePin(endAttr);
		if (a.output == b.output)
			return false; // need exactly one output and one input
		const DecodedPin& out = a.output ? a : b;
		const DecodedPin& in = a.output ? b : a;
		return flow::edit::connectReplacing(graph, out.node, out.port, in.node, in.port);
	}

	// The currently-selected links as edges (Delete key). imnodes link ids are edge
	// indices, so resolve them to Edge values here, before any mutation — flow::edit
	// then removes by stable identity, never by shifting index.
	static std::vector<flow::Graph::Edge> selectedEdges(const flow::Graph& graph)
	{
		std::vector<flow::Graph::Edge> out;
		const int selected = gui::nodes::NumSelectedLinks();
		if (selected <= 0)
			return out;

		std::vector<int> linkIds(static_cast<std::size_t>(selected));
		gui::nodes::GetSelectedLinks(linkIds.data());
		const std::vector<flow::Graph::Edge>& edges = graph.edges();
		for (const int id : linkIds)
		{
			if (id >= 0 && static_cast<std::size_t>(id) < edges.size())
				out.push_back(edges[static_cast<std::size_t>(id)]);
		}
		return out;
	}

	// The currently-selected nodes as ids (Delete key). imnodes node ids are the int
	// cast of NodeId.
	static std::vector<flow::NodeId> selectedNodes()
	{
		std::vector<flow::NodeId> out;
		const int selected = gui::nodes::NumSelectedNodes();
		if (selected <= 0)
			return out;

		std::vector<int> nodeIds(static_cast<std::size_t>(selected));
		gui::nodes::GetSelectedNodes(nodeIds.data());
		for (const int id : nodeIds)
			out.push_back(flow::NodeId{static_cast<std::uint64_t>(id)});
		return out;
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

	ImTextureID InspectorWindow::previewFor(const acm::Texture& texture)
	{
		const VkImageView view = texture.vkImageView();
		const auto it = m_previews.find(view);
		if (it != m_previews.end())
			return it->second;
		// A node reuses its texture (stable view) across recomputes, so this registers
		// once per texture and is reused thereafter. A deleted node's entry lingers but
		// is never drawn (we only look up currently-live textures) — see onShutdown.
		return m_previews.emplace(view, m_guiCtx->image(texture, m_sampler)).first->second;
	}

	void InspectorWindow::prunePreviews(const flow::Graph& graph)
	{
		// Collect the image views still held by a live port (an output, or a downstream
		// input that kept its last value). Anything cached but no longer present is a
		// texture that's gone — release its descriptor back to the pool.
		std::set<VkImageView> live;
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			const auto collect = [&](const flow::Port& p)
			{
				if (p.ready() && p.type() == typeid(acm::Texture))
					live.insert(p.value().get<acm::Texture>().vkImageView());
			};
			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				collect(node.input(i));
			for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
				collect(node.output(o));
		}

		for (auto it = m_previews.begin(); it != m_previews.end();)
		{
			if (live.count(it->first) == 0)
			{
				m_guiCtx->releaseImage(it->second);
				it = m_previews.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void InspectorWindow::onRender(app::Window& window, const app::TimeState&)
	{
		FlowviewApp& appDelegate = window.app().getDelegate<FlowviewApp>();
		flow::Graph& graph = appDelegate.graph(); // mutated by the canvas below

		m_guiCtx->newFrame();

		// The node canvas is drawn (and edited) BEFORE the inspector panel, so a node
		// deletion takes effect before the panel reads the graph and re-registers its
		// texture preview — the panel never references a just-freed node's texture.
		gui::SetNextWindowPos(math::Vec2f{360.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{880.0f, 600.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Graph");
		// Let a link be detached by click-dragging it off a pin: drop it in empty space
		// to remove it, or on another pin to move it (both surface via IsLinkDestroyed /
		// IsLinkCreated after EndNodeEditor).
		gui::nodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
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
		gui::nodes::PopAttributeFlag();

		// Canvas editing (must query imnodes after EndNodeEditor): a detached or dragged
		// link disconnects/connects; Delete removes the selected links + nodes; a
		// right-click adds a node from the factory palette at the cursor.
		// imnodes runs inside a child window, so focus/hover checks must include child
		// windows — otherwise, keyboard + the add popup only work after a stray click
		// that moves focus to the outer window.
		const bool canvasActive = gui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		bool edited = false;
		// A detached link (dropped in empty space, or the "off" side of a move). Handle
		// it before IsLinkCreated so a move frees the input before the reattach lands.
		int destroyedLink = 0;
		if (gui::nodes::IsLinkDestroyed(&destroyedLink) && destroyedLink >= 0 && static_cast<std::size_t>(destroyedLink) < edges.size())
		{
			const flow::Graph::Edge e = edges[static_cast<std::size_t>(destroyedLink)];
			edited |= flow::edit::disconnect(graph, e.to, e.inPort);
		}
		int startAttr = 0;
		int endAttr = 0;
		if (gui::nodes::IsLinkCreated(&startAttr, &endAttr))
			edited |= tryConnect(graph, startAttr, endAttr);
		if (canvasActive && gui::IsKeyPressed(ImGuiKey_Delete))
		{
			// Resolve the selection to stable values first, then delete in one edit.
			const std::vector<flow::Graph::Edge> edgesToRemove = selectedEdges(graph);
			const std::vector<flow::NodeId> nodesToRemove = selectedNodes();
			if (flow::edit::remove(graph, nodesToRemove, edgesToRemove))
			{
				// Drop imnodes' now-stale selection: link ids are edge indices, which
				// shift once an edge is removed, so a leftover selection could later
				// resolve to the wrong edge.
				gui::nodes::ClearLinkSelection();
				gui::nodes::ClearNodeSelection();
				edited = true;
			}
		}
		// Right-click empty canvas -> add-node palette (the imnodes color_node_editor
		// pattern: focus + editor hover + mouse release).
		if (canvasActive && gui::nodes::IsEditorHovered() && gui::IsMouseReleased(ImGuiMouseButton_Right))
			gui::OpenPopup("addNode");
		if (gui::BeginPopup("addNode"))
		{
			const ImVec2 mouse = gui::GetMousePosOnOpeningCurrentPopup();
			for (const std::string& key : appDelegate.nodeFactory().keys())
			{
				if (gui::MenuItem(key.c_str()))
				{
					const flow::NodeId id = flow::edit::addNode(graph, appDelegate.nodeFactory().create(key));
					gui::nodes::SetNodeScreenSpacePos(static_cast<int>(id.value()), math::Vec2f{mouse.x, mouse.y});
					edited = true;
				}
			}
			gui::EndPopup();
		}
		gui::End();
		m_laidOut = true;

		// A topology edit changes what should flow; re-run the scene, then reclaim any
		// preview descriptors whose texture is now gone. The cache is keyed by texture,
		// so the panel below just looks up whatever the current nodes carry.
		if (edited)
		{
			appDelegate.reevaluate();
			prunePreviews(graph);
		}

		// The inspector panel — reads the (now post-edit) graph.
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
					if (texture.valid())
					{
						const float side = previewExtent(m_previewSize);
						gui::Image(previewFor(texture), math::Vec2f{side, side}); // each texture port previews its own current texture
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

		window.renderer().render([&](acm::CommandBuffer cmd, uint32_t)
								 { m_guiCtx->render(cmd); });
	}

	void InspectorWindow::onShutdown(app::Window&)
	{
		m_guiCtx.reset();	// destroy the ImGui backends before the device tears down
		m_sampler = {};		// drop the sampler handle
		m_previews.clear(); // ids are freed with the backend's descriptor pool above
	}
} // namespace flowview
