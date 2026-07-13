#include "inspectorwindow.h"

#include "flowviewapp.h"
#include "graphio.h"

#include <archimedes/archimedes.h>
#include <lain/data/value.h>
#include <lain/app/application.h>
#include <lain/app/window.h>
#include <lain/flow/boundary.h>
#include <lain/flow/dynamicports.h>
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/enums.h>
#include <lain/gui/gui.h>
#include <lain/gui/nodes.h>
#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/log/log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// A pin's editor id, in imnodes' attribute id-space (separate from node + link ids).
	// node * 1000 + (output ? 500 : 0) + PortId keeps inputs and outputs distinct; assumes
	// fewer than 500 ports per direction and node ids well under ~2M — fine for prototyping.
	// Encodes the port's stable PortId (not its index), so a pin's editor id survives sibling
	// pins being added/removed.
	static int pinId(flow::NodeId node, bool output, flow::PortId port)
	{
		return static_cast<int>(node.value()) * 1000 + (output ? 500 : 0) + static_cast<int>(port.value());
	}

	// Inverse of pinId(): decode an imnodes attribute id back to the (node, direction,
	// port) it stands for — used when a dragged link reports its endpoint pins.
	struct DecodedPin
	{
		flow::NodeId node;
		bool output;
		flow::PortId port;
	};

	static DecodedPin decodePin(int attr)
	{
		const int rem = attr % 1000;
		const bool output = rem >= 500;
		return DecodedPin{
			flow::NodeId{static_cast<std::uint64_t>(attr / 1000)},
			output,
			flow::PortId{static_cast<std::uint32_t>(output ? rem - 500 : rem)}};
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
		return flow::edit::connectReplacing(graph, flow::PortAddress{out.node, out.port},
											flow::PortAddress{in.node, in.port});
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
		const auto refresh = [&](flow::NodeId id, const flow::Port& p, bool output)
		{
			if (!p.ready() || p.type() != typeid(image::Image))
				return;
			const image::Image& img = p.value().get<image::Image>();
			if (!img.valid())
				return;
			const PinKey key{id.value(), output, p.id()};
			live.insert(key);
			gui::Texture& tex = m_previews[key];	// default-empty on first sight
			if (!tex.upload(img))					// re-upload in place when size/format fits...
				tex = m_guiCtx->createTexture(img); // ...else first-time or resized -> reallocate
		};
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				refresh(id, node.input(i), false);
			for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
				refresh(id, node.output(o), true);
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

	void InspectorWindow::renderImageSave(const PinKey& key, const image::Image& img)
	{
		const std::vector<std::string> formats = savableFormats(img);
		if (formats.empty())
		{
			// Nothing can store this image losslessly (e.g. RGBA/float with only JPEG); an
			// explicit convert is the fix, not a silent degrade (ADR-0003).
			gui::TextDisabled("(no lossless format — convert first)");
			return;
		}
		// The chosen format for this pin, defaulting to (and falling back to) the first savable
		// when unset or no longer offered.
		std::string& sel = m_saveFormat[key];
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

	// Number of edges touching a port (its incident links) — shown in the remove confirmation.
	static int linkCount(const flow::Graph& graph, flow::PortAddress port)
	{
		int n = 0;
		for (const flow::Graph::Edge& e : graph.edges())
			if (e.from == port || e.to == port)
				++n;
		return n;
	}

	// An editable pin-name field (commits on Enter / focus loss) — boundary pins are user-named.
	static void renderPinName(flow::Port& pin)
	{
		char buffer[128];
		std::snprintf(buffer, sizeof(buffer), "%s", pin.name().c_str());
		gui::SetNextItemWidth(110.0f);
		gui::InputText("##name", buffer, sizeof(buffer));
		if (gui::IsItemDeactivatedAfterEdit() && flow::validPortName(buffer))
			pin.setName(buffer); // an invalid name is refused — the field reverts to the current name next frame
	}

	// The per-node "+" : a menu of the registered port types the node accepts (filtered by
	// acceptsPortType); picking one adds a pin via the edit seam, auto-named `<prefix><count>`.
	static bool renderAddPin(flow::Graph& graph, flow::DynamicPortsNode& node, const char* prefix)
	{
		bool added = false;
		if (gui::Button("+ add pin"))
			gui::OpenPopup("addPin");
		if (gui::BeginPopup("addPin"))
		{
			bool any = false;
			for (const std::string& key : flow::portTypeKeys())
			{
				if (!node.acceptsPortType(key))
					continue;
				any = true;
				if (gui::MenuItem(key.c_str()))
				{
					const std::size_t count = node.dynamicSide() == flow::Port::Direction::Output ? node.outputCount()
																								  : node.inputCount();
					flow::edit::addPort(graph, node.id(), key, std::string(prefix) + std::to_string(count));
					added = true;
				}
			}
			if (!any)
				gui::TextDisabled("(no registered types)");
			gui::EndPopup();
		}
		return added;
	}

	// A dynamic node drawn on the CANVAS gets a small "+ <type>" per accepted port type (a
	// homogeneous Merge/Select shows one; a multi-type node one each). A click records a deferred add
	// — applied after EndNodeEditor. Takes the node const + only appends to `pinAdds`, so it is safe to
	// call mid-draw (the graph mutation happens later). PushID(node) keeps same-labelled buttons on
	// different nodes distinct.
	static void renderCanvasAddPin(const flow::DynamicPortsNode& node, flow::NodeId id,
								   std::vector<std::pair<flow::NodeId, std::string>>& pinAdds)
	{
		gui::PushID(static_cast<int>(id.value()));
		for (const std::string& key : flow::portTypeKeys())
		{
			if (!node.acceptsPortType(key))
				continue;
			if (gui::SmallButton(("+ " + key).c_str()))
				pinAdds.emplace_back(id, key);
		}
		gui::PopID();
	}

	bool InspectorWindow::renderRemoveConfirm(flow::Graph& graph)
	{
		if (m_removeRequested)
		{
			gui::OpenPopup("Remove pin");
			m_removeRequested = false;
		}
		bool removed = false;
		if (gui::BeginPopupModal("Remove pin", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			gui::Text("Remove '%s'?  It has %d link(s).", m_removeName.c_str(), m_removeLinks);
			if (gui::Button("Remove"))
			{
				removed = flow::edit::removePort(graph, m_removeTarget);
				gui::CloseCurrentPopup();
			}
			gui::SameLine();
			if (gui::Button("Cancel"))
				gui::CloseCurrentPopup();
			gui::EndPopup();
		}
		return removed;
	}

	void InspectorWindow::renderInterfacePanel(FlowviewApp& appDelegate)
	{
		flow::Graph& graph = appDelegate.graph();
		const float side = previewExtent(m_previewSize);
		bool changed = false;

		// Request the remove-confirm for `pin` on `node` (opened after the panel, clean id stack).
		const auto requestRemove = [&](flow::NodeId node, const flow::Port& pin)
		{
			m_removeTarget = flow::PortAddress{node, pin.id()};
			m_removeName = pin.name();
			m_removeLinks = linkCount(graph, m_removeTarget);
			m_removeRequested = true;
		};

		gui::SetNextWindowPos(math::Vec2f{940.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 520.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Interface");

		// Inputs: per GroupInput node, each pin (editable name, bound thumbnail, Bind…, ×) + a "+".
		gui::TextUnformatted("Inputs");
		gui::Separator();
		if (flow::GroupInputNode* node = graph.boundaryInputNode())
		{
			gui::PushID(static_cast<int>(node->id().value()));
			for (flow::PortIndex i = 0; i < node->outputCount(); ++i)
			{
				flow::Port& pin = node->output(i);
				gui::PushID(static_cast<int>(pin.id().value()));
				renderPinName(pin);
				gui::SameLine();
				gui::Text(": %s", std::string(pin.typeName()).c_str());

				const PinKey key{node->id().value(), true, pin.id()};
				const auto it = m_previews.find(key);
				if (it != m_previews.end() && it->second.valid())
					gui::Image(it->second, math::Vec2f{side, side});

				if (pin.type() == typeid(image::Image) && gui::Button("Bind file..."))
				{
					if (const auto path = gui::openFile("Open image", {},
														{{"Images", {"*.png", "*.jpg", "*.jpeg", "*.tif", "*.tiff"}}}))
					{
						if (auto image = io::image::load(path->string()))
						{
							flow::PortValue v;
							v.set<image::Image>(std::move(*image));
							node->setValue(pin.id(), std::move(v));
							changed = true;
						}
						else
						{
							gui::message("Load failed", "Could not load: " + path->string(), true);
						}
					}
				}
				gui::SameLine();
				if (gui::Button("x"))
					requestRemove(node->id(), pin);
				gui::PopID();
			}
			changed |= renderAddPin(graph, *node, "input");
			gui::PopID();
		}

		// Outputs: per GroupOutput node, each pin (editable name, result thumbnail, Save…, ×) + "+".
		gui::Spacing();
		gui::TextUnformatted("Outputs");
		gui::Separator();
		if (flow::GroupOutputNode* node = graph.boundaryOutputNode())
		{
			gui::PushID(static_cast<int>(node->id().value()));
			for (flow::PortIndex i = 0; i < node->inputCount(); ++i)
			{
				flow::Port& pin = node->input(i);
				gui::PushID(static_cast<int>(pin.id().value()));
				renderPinName(pin);
				gui::SameLine();
				gui::Text(": %s", std::string(pin.typeName()).c_str());

				const PinKey key{node->id().value(), false, pin.id()};
				if (pin.value().empty())
				{
					// The producer was gated off / suppressed (conditional eval) — no value this run.
					// refreshPreviews already dropped any stale thumbnail; say so rather than show blank.
					gui::TextDisabled("(no output this run)");
				}
				else
				{
					const auto it = m_previews.find(key);
					if (it != m_previews.end() && it->second.valid())
						gui::Image(it->second, math::Vec2f{side, side});
					if (pin.value().holds<image::Image>())
						renderImageSave(key, pin.value().get<image::Image>());
				}
				gui::SameLine();
				if (gui::Button("x"))
					requestRemove(node->id(), pin);
				gui::PopID();
			}
			changed |= renderAddPin(graph, *node, "output");
			gui::PopID();
		}
		gui::End();

		changed |= renderRemoveConfirm(graph);

		// An add / remove / bind re-runs the graph (as a param/canvas edit does) and refreshes
		// previews next frame.
		if (changed)
		{
			appDelegate.reevaluate();
			m_previewsDirty = true;
		}
	}

	// The canvas layout as EditorData: each node's grid-space position as { x, y }. Round-tripped
	// through the document's "editor" section, which flow::serialize treats opaquely.
	static flow::serialize::EditorData collectLayout(const flow::Graph& graph)
	{
		flow::serialize::EditorData layout;
		for (const flow::NodeId id : graph.nodeIds())
		{
			const ImVec2 pos = gui::nodes::GetNodeGridSpacePos(static_cast<int>(id.value()));
			data::Value blob = data::Value::object();
			blob.set("x", data::Value(static_cast<double>(pos.x)));
			blob.set("y", data::Value(static_cast<double>(pos.y)));
			layout[id] = std::move(blob);
		}
		return layout;
	}

	// Apply a saved position to node `id` if `layout` has one; returns whether it did (so the caller
	// falls back to the default column layout otherwise).
	static bool applyLayout(const flow::serialize::EditorData& layout, flow::NodeId id)
	{
		const auto it = layout.find(id);
		if (it == layout.end())
			return false;
		const data::Value* x = it->second.find("x");
		const data::Value* y = it->second.find("y");
		gui::nodes::SetNodeGridSpacePos(static_cast<int>(id.value()),
										math::Vec2f{static_cast<float>(x ? x->asDouble().value_or(0.0) : 0.0),
													static_cast<float>(y ? y->asDouble().value_or(0.0) : 0.0)});
		return true;
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

		// Save / Load the whole scene as JSON (recipe + canvas layout). Native dialogs block the
		// render thread — the same pattern as the per-output Save… below.
		if (gui::Button("Save…"))
		{
			if (const auto path = gui::saveFile("Save graph", {}, {{"json", {"*.json"}}}))
			{
				std::filesystem::path file = *path;
				file.replace_extension("json"); // force .json (the codec is keyed off the extension)
				if (!saveGraph(file.string(), graph, appDelegate.nodeFactory(), collectLayout(graph)))
					gui::message("Save failed", "Could not write " + file.string(), true);
			}
		}
		gui::SameLine();
		if (gui::Button("Load…"))
		{
			// "All files" fallback: pfd 0.1.0's macOS picker can grey out everything under a lone
			// restrictive filter, so offer an escape hatch alongside the JSON one.
			if (const auto path = gui::openFile("Load graph", {}, {{"JSON graph", {"*.json"}}, {"All files", {"*"}}}))
			{
				flow::serialize::LoadResult result = loadGraph(path->string(), appDelegate.nodeFactory());
				if (!result.clean())
				{
					std::string summary;
					for (const auto& issue : result.issues)
						summary += "• " + issue.message + "\n";
					gui::message("Graph loaded with issues", summary, true);
				}
				if (result.graph.nodeCount() > 0) // replace the scene — deferred to end of frame
				{
					m_loadedGraph = std::make_unique<flow::Graph>(std::move(result.graph));
					m_pendingLayout = std::move(result.editor);
					m_loadRequested = true;
				}
			}
		}

		// Link detach (click-drag a link off a pin to remove/move it) is enabled on INPUT pins
		// only — see the per-node loop. Leaving OUTPUT pins without the flag lets a drag *from* an
		// output start a NEW link, so one output fans out to many inputs (an input, being
		// single-source, is where detach-to-move belongs). Detaches/reattaches surface via
		// IsLinkDestroyed / IsLinkCreated after EndNodeEditor.
		gui::nodes::BeginNodeEditor();
		const bool seedPositions = !m_laidOut; // captured before the loop: this is a position-seed frame
		// Canvas ± requests, applied after EndNodeEditor so the graph isn't mutated mid-draw (node id +
		// the picked port-type key). A palette-placed dynamic node (Merge/Select) grows its branches here;
		// boundary nodes are grown from the Interface panel, but the same affordance works on-canvas too.
		std::vector<std::pair<flow::NodeId, std::string>> pinAdds;
		int column = 0;
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			if (seedPositions && !applyLayout(m_pendingLayout, id)) // a loaded layout wins over the default columns
				gui::nodes::SetNodeGridSpacePos(static_cast<int>(id.value()), math::Vec2f{column * 220.0f, 40.0f + (column % 4) * 140.0f});

			gui::nodes::BeginNode(static_cast<int>(id.value()));
			gui::nodes::BeginNodeTitleBar();
			gui::Text("[%llu] %s", static_cast<unsigned long long>(id.value()), node.name().c_str());
			gui::nodes::EndNodeTitleBar();

			// Detach flag on inputs only (so an output drag creates a new link -> fan-out).
			gui::nodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& in = node.input(i);
				gui::nodes::BeginInputAttribute(pinId(id, false, in.id()));
				gui::Text("%s : %s", in.name().c_str(), std::string(in.typeName()).c_str());
				gui::nodes::EndInputAttribute();
			}
			gui::nodes::PopAttributeFlag();
			// A dynamic node that grows its INPUT side gets its ± below the inputs.
			const auto* dynamic = dynamic_cast<const flow::DynamicPortsNode*>(&node);
			if (dynamic != nullptr && dynamic->dynamicSide() == flow::Port::Direction::Input)
				renderCanvasAddPin(*dynamic, id, pinAdds);
			for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
			{
				const flow::Port& out = node.output(o);
				gui::nodes::BeginOutputAttribute(pinId(id, true, out.id()));
				gui::Text("%s : %s", out.name().c_str(), std::string(out.typeName()).c_str());
				gui::nodes::EndOutputAttribute();
			}
			// ... and one that grows its OUTPUT side gets its ± below the outputs.
			if (dynamic != nullptr && dynamic->dynamicSide() == flow::Port::Direction::Output)
				renderCanvasAddPin(*dynamic, id, pinAdds);
			gui::nodes::EndNode();
			++column;
		}

		const std::vector<flow::Graph::Edge>& edges = graph.edges();
		for (std::size_t e = 0; e < edges.size(); ++e)
			gui::nodes::Link(static_cast<int>(e), pinId(edges[e].from.node, true, edges[e].from.port),
							 pinId(edges[e].to.node, false, edges[e].to.port));

		gui::nodes::EndNodeEditor();

		// Canvas editing (must query imnodes after EndNodeEditor): a detached or dragged
		// link disconnects/connects; Delete removes the selected links + nodes; a
		// right-click adds a node from the factory palette at the cursor.
		// imnodes runs inside a child window, so focus/hover checks must include child
		// windows — otherwise, keyboard + the add popup only work after a stray click
		// that moves focus to the outer window.
		const bool canvasActive = gui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		bool edited = false;
		// Apply the canvas ± requests collected during the node draw (auto-named `<prefix><count>` on
		// the growing side), now that the graph can be mutated safely.
		for (const auto& [nodeId, key] : pinAdds)
		{
			const flow::DynamicPortsNode& dyn = static_cast<flow::DynamicPortsNode&>(graph.node(nodeId));
			const bool onOutput = dyn.dynamicSide() == flow::Port::Direction::Output;
			const std::size_t count = onOutput ? dyn.outputCount() : dyn.inputCount();
			const std::string name = std::string(onOutput ? "out" : "in") + std::to_string(count);
			if (flow::edit::addPort(graph, nodeId, key, name) != flow::PortId{})
				edited = true;
		}
		// A detached link (dropped in empty space, or the "off" side of a move). Handle
		// it before IsLinkCreated so a move frees the input before the reattach lands.
		int destroyedLink = 0;
		if (gui::nodes::IsLinkDestroyed(&destroyedLink) && destroyedLink >= 0 && static_cast<std::size_t>(destroyedLink) < edges.size())
		{
			const flow::Graph::Edge e = edges[static_cast<std::size_t>(destroyedLink)];
			edited |= flow::edit::disconnect(graph, e.to);
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
		if (seedPositions)
			m_pendingLayout.clear(); // only clear after the frame that actually consumed it (not the Load frame)

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
			bool nodeEdited = false;
			for (flow::PortIndex pi = 0; pi < node.paramCount(); ++pi)
				nodeEdited |= m_paramEditors.render(node.param(pi));
			if (nodeEdited)
				node.markDirty(); // a param edit -> incremental re-eval recomputes this node + downstream
			paramEdited |= nodeEdited;
			gui::PopID();

			auto port = [&](const char* tag, const flow::Port& p, bool output)
			{
				// A ready image port shows extent + its thumbnail (uploaded + cached by
				// refreshPreviews, keyed by the port's stable id); everything else — CPU values
				// and the empty slot — is text via the shared Port::describe() pathway.
				if (p.ready() && p.type() == typeid(image::Image))
				{
					const image::Image& img = p.value().get<image::Image>();
					gui::Text("    %s %s: %s %dx%d", tag, p.name().c_str(), std::string(p.typeName()).c_str(), img.width(), img.height());
					const PinKey key{id.value(), output, p.id()};
					const auto it = m_previews.find(key);
					if (it != m_previews.end() && it->second.valid())
					{
						const float side = previewExtent(m_previewSize);
						gui::Image(it->second, math::Vec2f{side, side}); // Texture -> ImTextureRef implicitly
					}
					if (output) // outputs are the results you'd export; inputs are just what was fed in
					{
						gui::PushID(pinId(id, output, p.id()));
						renderImageSave(key, img);
						gui::PopID();
					}
					return;
				}
				gui::Text("    %s %s: %s", tag, p.name().c_str(), p.describe().c_str());
			};

			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				port("in ", node.input(i), false);
			for (flow::PortIndex i = 0; i < node.outputCount(); ++i)
				port("out", node.output(i), true);
		}
		gui::End();

		// A param edit re-runs the scene (a full run recomputes every node) and marks the
		// previews for refresh next frame — the same path a canvas edit takes.
		if (paramEdited)
		{
			appDelegate.reevaluate();
			m_previewsDirty = true;
		}

		// The graph's I/O boundary — the host-binding surface (bind inputs, save outputs).
		renderInterfacePanel(appDelegate);

		// Apply a pending Load now — every panel has drawn with the current graph, so swapping it
		// here (not at the button) can't dangle the `graph` reference used above. Next frame re-seeds
		// positions from the loaded layout.
		if (m_loadRequested)
		{
			appDelegate.replaceGraph(std::move(m_loadedGraph));
			m_loadRequested = false;
			m_laidOut = false;
			m_previews.clear(); // the old graph's cached thumbnails are gone
			m_previewsDirty = true;
			gui::nodes::ClearNodeSelection();
			gui::nodes::ClearLinkSelection();
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
