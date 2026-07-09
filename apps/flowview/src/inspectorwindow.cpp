#include "inspectorwindow.h"

#include "flowviewapp.h"

#include <archimedes/archimedes.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/enums.h>
#include <lain/gui/gui.h>
#include <lain/gui/nodes.h>
#include <lain/image/image.h>
#include <lain/io/image/save.h>
#include <lain/log/log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

	// The registered write formats that can encode `img` WITHOUT loss — the writer keys
	// (alphabetical) filtered by io::image::canEncode. This is the format dropdown's contents, so a
	// format that would degrade the image (JPEG + alpha, any + float) simply isn't offered.
	static std::vector<std::string> savableFormats(const image::Image& img)
	{
		std::vector<std::string> out;
		for (const std::string& key : io::image::writerRegistry().keys())
			if (io::image::canEncode(key, img))
				out.push_back(key);
		return out;
	}

	// Write `img` as `key` (a savable format from the dropdown): pick where + name via the native
	// dialog, force the chosen format's extension (the dropdown is authoritative), and save. img is
	// read synchronously while the (blocking) dialog holds the main thread, so the port ref stays
	// valid. Success logs the path; a write failure raises an alert.
	static void saveImage(const image::Image& img, const std::string& key)
	{
		const auto path = gui::saveFile("Save image", {}, {{key, {"*." + key}}});
		if (!path)
			return; // cancelled
		std::filesystem::path out = *path;
		out.replace_extension(key);
		if (io::image::save(out.string(), img))
			log::info("flowview: saved image to {}", out.string());
		else
			gui::message("Save failed", "Couldn't write the file: " + out.string(), true);
	}

	bool InspectorWindow::onInit(app::Window& window)
	{
		m_guiCtx = std::make_unique<gui::Context>(window.app(), window);
		registerBuiltinParamEditors(m_paramEditors);
		return true;
	}

	void InspectorWindow::refreshPreviews(const flow::Graph& graph)
	{
		std::set<PinKey> live;
		const auto refresh = [&](flow::NodeId id, const flow::Port& p, bool output, flow::PortIndex index)
		{
			if (!p.ready() || p.type() != typeid(image::Image))
				return;
			const image::Image& img = p.value().get<image::Image>();
			if (!img.valid())
				return;
			const PinKey key{id.value(), output, index};
			live.insert(key);
			gui::Texture& tex = m_previews[key];	// default-empty on first sight
			if (!tex.upload(img))					// re-upload in place when size/format fits...
				tex = m_guiCtx->createTexture(img); // ...else first-time or resized -> reallocate
		};
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				refresh(id, node.input(i), false, i);
			for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
				refresh(id, node.output(o), true, o);
		}
		// Drop previews whose pin is gone (or no longer a ready image); the erased
		// gui::Texture reclaims its descriptor.
		for (auto it = m_previews.begin(); it != m_previews.end();)
		{
			if (live.count(it->first) == 0)
				it = m_previews.erase(it);
			else
				++it;
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

		// Persistent node palette: a left-click list of the factory's node types — trackpad-
		// native, where the right-click add menu above (kept as a secondary) is awkward on a
		// MacBook. A new node cascades its grid position so successive adds don't stack.
		gui::SetNextWindowPos(math::Vec2f{20.0f, 360.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 230.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Nodes");
		for (const std::string& key : appDelegate.nodeFactory().keys())
		{
			if (gui::Button(key.c_str()))
			{
				const flow::NodeId newId = flow::edit::addNode(graph, appDelegate.nodeFactory().create(key));
				const float offset = 40.0f + static_cast<float>(m_addCounter % 6) * 28.0f;
				gui::nodes::SetNodeGridSpacePos(static_cast<int>(newId.value()), math::Vec2f{offset, offset});
				++m_addCounter;
				edited = true;
			}
		}
		gui::End();

		// A topology edit re-runs the scene, so the previews need refreshing (recomputed
		// images, added/removed ports). Mark them dirty; refreshPreviews below upserts in
		// place where it can. (Deferred, not per-frame — acm::Texture::upload is a stalling
		// synchronous submit.)
		if (edited)
		{
			appDelegate.reevaluate();
			m_previewsDirty = true;
		}

		if (m_previewsDirty)
		{
			refreshPreviews(graph);
			m_previewsDirty = false;
		}

		// The inspector panel — reads the (now post-edit) graph.
		gui::SetNextWindowPos(math::Vec2f{20.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 320.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Inspector");
		gui::enumCombo("Preview size", m_previewSize); // labels from lain::meta::enums
		bool paramEdited = false;
		for (const flow::NodeId id : graph.topoOrder())
		{
			flow::Node& node = graph.node(id); // non-const: params are edited below
			gui::Text("[%llu] %s", static_cast<unsigned long long>(id.value()), node.name().c_str());

			// Editable params, chosen by type via the registry (file field, drags, colour
			// swatch). PushID(node) so same-named params on different nodes don't collide.
			gui::PushID(static_cast<int>(id.value()));
			for (flow::PortIndex pi = 0; pi < node.paramCount(); ++pi)
				paramEdited |= m_paramEditors.render(node.param(pi));
			gui::PopID();

			auto port = [&](const char* tag, const flow::Port& p, bool output, flow::PortIndex index)
			{
				// A ready image port shows extent + its thumbnail (uploaded + cached by
				// refreshPreviews, keyed by pin); everything else — CPU values and the empty
				// slot — is text via the shared Port::describe() pathway.
				if (p.ready() && p.type() == typeid(image::Image))
				{
					const image::Image& img = p.value().get<image::Image>();
					gui::Text("    %s %s: %s %dx%d", tag, p.name().c_str(), std::string(p.typeName()).c_str(), img.width(), img.height());
					const PinKey key{id.value(), output, index};
					const auto it = m_previews.find(key);
					if (it != m_previews.end() && it->second.valid())
					{
						const float side = previewExtent(m_previewSize);
						gui::Image(it->second, math::Vec2f{side, side}); // Texture -> ImTextureRef implicitly
					}
					if (output) // outputs are the results you'd export; inputs are just what was fed in
					{
						gui::PushID(pinId(id, output, index));
						const std::vector<std::string> formats = savableFormats(img);
						if (formats.empty())
						{
							// Nothing can store this image losslessly (e.g. RGBA/float with only JPEG);
							// an explicit convert is the fix, not a silent degrade (ADR-0003).
							gui::TextDisabled("    (no lossless format — convert first)");
						}
						else
						{
							// The chosen format for this pin, defaulting to (and falling back to) the
							// first savable when unset or no longer offered.
							std::string& sel = m_saveFormat[PinKey{id.value(), output, index}];
							if (std::find(formats.begin(), formats.end(), sel) == formats.end())
								sel = formats.front();
							gui::SetNextItemWidth(80.0f);
							if (gui::BeginCombo("##fmt", sel.c_str()))
							{
								for (const std::string& f : formats)
									if (gui::Selectable(f.c_str(), f == sel))
										sel = f;
								gui::EndCombo();
							}
							gui::SameLine();
							if (gui::Button("Save..."))
								saveImage(img, sel);
						}
						gui::PopID();
					}
					return;
				}
				gui::Text("    %s %s: %s", tag, p.name().c_str(), p.describe().c_str());
			};

			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				port("in ", node.input(i), false, i);
			for (flow::PortIndex i = 0; i < node.outputCount(); ++i)
				port("out", node.output(i), true, i);
		}
		gui::End();

		// A param edit re-runs the scene (a full run recomputes every node) and marks the
		// previews for refresh next frame — the same path a canvas edit takes.
		if (paramEdited)
		{
			appDelegate.reevaluate();
			m_previewsDirty = true;
		}

		window.renderer().render([&](acm::CommandBuffer cmd, uint32_t)
								 { m_guiCtx->render(cmd); });
	}

	void InspectorWindow::onShutdown(app::Window&)
	{
		m_previews.clear(); // release the preview descriptors while the ImGui backend lives
		m_guiCtx.reset();	// then destroy the backend, before the device tears down
	}
} // namespace flowview
