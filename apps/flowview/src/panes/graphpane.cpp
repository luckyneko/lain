#include "graphpane.h"

#include "../appcontext.h"
#include "../flowviewapp.h" // ctx.app->nodeFactory()
#include "../scene.h"		 // nodeCatalog + NodeCategory
#include "canvasids.h"		 // pinId + selectedNodes

#include <lain/data/value.h>
#include <lain/flow/boundary.h> // GroupInputNode / GroupOutputNode
#include <lain/flow/dynamicports.h>
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/serialize/loadresult.h> // EditorData (applyLayout)
#include <lain/gui/color.h>					// gui::packColor
#include <lain/gui/gui.h>
#include <lain/gui/nodes.h>
#include <lain/image/image.h>
#include <lain/math/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

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

	// A node by id, or nullptr if it isn't in the graph — for a decoded hovered/dragged pin, whose node
	// may have been deleted earlier this frame (graph.node() would throw). O(nodes), fine off the hot path.
	static const flow::Node* findNode(const flow::Graph& graph, flow::NodeId id)
	{
		for (const flow::NodeId nid : graph.nodeIds())
		{
			if (nid == id)
				return &graph.node(id);
		}
		return nullptr;
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

	void GraphPane::init()
	{
		registerBuiltinCanvasStyle(m_style);
		// Trackpad-friendly canvas panning: Alt + left-drag pans (imnodes' default pan is middle-mouse,
		// awkward on a laptop). The gui Context created the imnodes context this reads.
		gui::nodes::GetIO().EmulateThreeButtonMouse.Modifier = &gui::GetIO().KeyAlt;
	}

	void GraphPane::onGraphReplaced()
	{
		m_laidOut = false; // re-seed positions from the loaded layout next frame
		gui::nodes::ClearNodeSelection();
		gui::nodes::ClearLinkSelection();
	}

	void GraphPane::draw(AppContext& ctx, flow::Graph& graph, bool& edited)
	{
		gui::SetNextWindowPos(math::Vec2f{360.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{880.0f, 600.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Graph");
		const ImVec2 canvasSize = gui::GetContentRegionAvail(); // captured before the editor: to centre a located node

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

		// Default layout (seed only): Input node leftmost, Output node rightmost, everything else in a
		// row between — so a fresh graph reads left-to-right. Can't key off topoOrder *position*: its
		// LIFO drain can order Output before Input among edgeless nodes.
		const int lastColumn = static_cast<int>(graph.topoOrder().size()) - 1;
		int middleColumn = 0; // next column for a non-boundary node

		// Whether a pin renders muted. A pin ALWAYS shows its type colour otherwise — the node's dim
		// title/background and its muted dead links carry the inactive state, so muting the pins too
		// would only hide the type you need to wire an incomplete node. The one exception is a link
		// drag: mute every pin that isn't a valid drop target (opposite direction + same type).
		const auto pinMuted = [&](bool pinIsOutput, std::type_index type)
		{
			if (m_linkDragActive)
				return !(pinIsOutput != m_linkDragFromOutput && type == m_linkDragType);
			return false;
		};

		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			// Default layout by role (a loaded layout wins over it): Input -> leftmost, Output ->
			// rightmost, others fill the columns between.
			if (seedPositions && !applyLayout(ctx.pendingLayout, id))
			{
				int col = 1 + middleColumn;
				if (dynamic_cast<const flow::GroupInputNode*>(&node) != nullptr)
					col = 0;
				else if (dynamic_cast<const flow::GroupOutputNode*>(&node) != nullptr)
					col = lastColumn;
				else
					++middleColumn;
				gui::nodes::SetNodeGridSpacePos(static_cast<int>(id.value()), math::Vec2f{60.0f + col * 240.0f, 200.0f});
			}

			// State (dim) trumps identity (category colour): an inactive node — one the run skipped
			// because a Required input was empty (ADR-0007) — is fully muted; an active one wears its
			// kind's title colour. Push counted so the pop matches whichever branch ran.
			const bool active = node.ready(); // an unready node (a Required input empty) was skipped -> dim
			int nodeColoursPushed = 0;
			const auto pushNodeColour = [&](ImNodesCol slot, const image::ColorRGBA8& colour)
			{
				gui::nodes::PushColorStyle(slot, gui::packColor(colour)); // pack at the imnodes boundary
				++nodeColoursPushed;
			};
			if (active)
			{
				const image::ColorRGBA8 title = m_style.nodeTitle(ctx.app->nodeFactory().keyOf(node));
				pushNodeColour(ImNodesCol_TitleBar, title);
				pushNodeColour(ImNodesCol_TitleBarHovered, title);
				pushNodeColour(ImNodesCol_TitleBarSelected, m_style.selection()); // selected -> accent
			}
			else
			{
				const image::ColorRGBA8 mutedTitle = m_style.mutedTitle();
				const image::ColorRGBA8 mutedBg = m_style.mutedBackground();
				pushNodeColour(ImNodesCol_TitleBar, mutedTitle);
				pushNodeColour(ImNodesCol_TitleBarHovered, mutedTitle);
				pushNodeColour(ImNodesCol_TitleBarSelected, m_style.selection()); // selected -> accent
				pushNodeColour(ImNodesCol_NodeBackground, mutedBg);
				pushNodeColour(ImNodesCol_NodeBackgroundHovered, mutedBg);
				pushNodeColour(ImNodesCol_NodeBackgroundSelected, mutedBg);
			}

			// A STABLE content-column width from label text (title + pin names). Right-aligning outputs
			// to the node's *rendered* width feeds back — the indent widens the node, which widens next
			// frame's indent → runaway growth; a text-derived width is fixed per frame.
			char titleText[128];
			std::snprintf(titleText, sizeof(titleText), "%s [%llu]", node.name().c_str(), static_cast<unsigned long long>(id.value()));
			float labelColumn = gui::CalcTextSize(titleText).x;
			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
				labelColumn = std::max(labelColumn, gui::CalcTextSize(node.input(i).name().c_str()).x);
			for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
				labelColumn = std::max(labelColumn, gui::CalcTextSize(node.output(o).name().c_str()).x);

			gui::nodes::BeginNode(static_cast<int>(id.value()));
			gui::nodes::BeginNodeTitleBar();
			gui::TextUnformatted(titleText);
			gui::nodes::EndNodeTitleBar();

			// Detach flag on inputs only (so an output drag creates a new link -> fan-out).
			gui::nodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& in = node.input(i);
				// Fill = carries a value, hollow = empty (unconnected, or upstream produced nothing) —
				// connectedness is read from the wire. Colour = type always (its label is just the name;
				// type + value are in the hover tooltip).
				const ImNodesPinShape shape = in.ready() ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle;
				gui::nodes::PushColorStyle(ImNodesCol_Pin, gui::packColor(pinMuted(false, in.type()) ? m_style.mutedPin() : m_style.portColor(in.type(), in.typeName())));
				gui::nodes::BeginInputAttribute(pinId(id, false, in.id()), shape);
				gui::TextUnformatted(in.name().c_str());
				gui::nodes::EndInputAttribute();
				gui::nodes::PopColorStyle();
			}
			gui::nodes::PopAttributeFlag();
			// A dynamic node that grows its INPUT side gets its ± below the inputs.
			const auto* dynamic = dynamic_cast<const flow::DynamicPortsNode*>(&node);
			if (dynamic != nullptr && dynamic->dynamicSide() == flow::Port::Direction::Input)
				renderCanvasAddPin(*dynamic, id, pinAdds);
			for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
			{
				const flow::Port& out = node.output(o);
				// Fill = produced a value this run, hollow = empty (a suppressed node's outputs) — same
				// value-presence rule as inputs.
				const ImNodesPinShape shape = out.ready() ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle;
				gui::nodes::PushColorStyle(ImNodesCol_Pin, gui::packColor(pinMuted(true, out.type()) ? m_style.mutedPin() : m_style.portColor(out.type(), out.typeName())));
				gui::nodes::BeginOutputAttribute(pinId(id, true, out.id()), shape);
				// Right-align the label to the node's stable content column so it sits by the right-edge pin.
				const float pad = labelColumn - gui::CalcTextSize(out.name().c_str()).x;
				if (pad > 0.0f)
					gui::Indent(pad);
				gui::TextUnformatted(out.name().c_str());
				if (pad > 0.0f)
					gui::Unindent(pad);
				gui::nodes::EndOutputAttribute();
				gui::nodes::PopColorStyle();
			}
			// ... and one that grows its OUTPUT side gets its ± below the outputs.
			if (dynamic != nullptr && dynamic->dynamicSide() == flow::Port::Direction::Output)
				renderCanvasAddPin(*dynamic, id, pinAdds);
			gui::nodes::EndNode();

			for (int k = 0; k < nodeColoursPushed; ++k)
				gui::nodes::PopColorStyle();
		}

		// Links carry their source pin's type colour, muted when the source produced nothing (a dead
		// edge downstream of a suppressed node).
		const std::vector<flow::Graph::Edge>& edges = graph.edges();
		for (std::size_t e = 0; e < edges.size(); ++e)
		{
			const flow::Graph::Edge& edge = edges[e];
			const flow::Port* src = graph.node(edge.from.node).findOutput(edge.from.port);
			const bool edgeActive = (src != nullptr) && src->ready();
			gui::nodes::PushColorStyle(ImNodesCol_Link, gui::packColor(edgeActive ? m_style.portColor(src->type(), src->typeName()) : m_style.mutedLink()));
			gui::nodes::Link(static_cast<int>(e), pinId(edge.from.node, true, edge.from.port), pinId(edge.to.node, false, edge.to.port));
			gui::nodes::PopColorStyle();
		}

		// A minimap (bottom-right) — an overview + click-to-navigate, so nodes panned off-screen aren't
		// lost (imnodes has no zoom; a minimap + panning is the mitigation until an imgui-node-editor
		// migration would add zoom).
		gui::nodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
		gui::nodes::EndNodeEditor();

		// Centre a located node (from an Issue click): pan so it sits at the canvas centre. Done here —
		// the node's grid position + size are only known once it has been drawn.
		if (ctx.locateTarget)
		{
			const int nid = static_cast<int>(ctx.locateTarget->value());
			const ImVec2 nodePos = gui::nodes::GetNodeGridSpacePos(nid);
			const ImVec2 nodeSize = gui::nodes::GetNodeDimensions(nid);
			gui::nodes::EditorContextResetPanning(ImVec2(canvasSize.x * 0.5f - (nodePos.x + nodeSize.x * 0.5f),
													 canvasSize.y * 0.5f - (nodePos.y + nodeSize.y * 0.5f)));
			ctx.locateTarget.reset();
		}

		// Canvas editing (must query imnodes after EndNodeEditor): a detached or dragged
		// link disconnects/connects; Delete removes the selected links + nodes; a
		// right-click adds a node from the factory palette at the cursor.
		// imnodes runs inside a child window, so focus/hover checks must include child
		// windows — otherwise, keyboard + the add popup only work after a stray click
		// that moves focus to the outer window.
		const bool canvasActive = gui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

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
		{
			if (tryConnect(graph, startAttr, endAttr))
				edited = true;
			else
				ctx.noteRejectedConnect(); // surface the silent rejection as a transient Issue
			m_linkDragActive = false;	   // the drag ended by forming a link
		}

		// Link-drag feedback: capture the source pin's direction + type on drag start (greys the
		// non-compatible pins from the next frame), and clear it when the drag ends.
		int dragAttr = 0;
		if (gui::nodes::IsLinkStarted(&dragAttr))
		{
			const DecodedPin d = decodePin(dragAttr);
			if (const flow::Node* n = findNode(graph, d.node))
			{
				const flow::Port* p = d.output ? n->findOutput(d.port) : n->findInput(d.port);
				if (p != nullptr)
				{
					m_linkDragActive = true;
					m_linkDragFromOutput = d.output;
					m_linkDragType = p->type();
				}
			}
		}
		if (m_linkDragActive && !gui::IsMouseDown(ImGuiMouseButton_Left))
			m_linkDragActive = false; // mouse released -> the drag is over (connected, dropped, or cancelled)

		// Pin tooltip: hovering a pin shows its full type + current value (the terse canvas label is
		// just the name). Its node may have been deleted this frame, so look it up guarded.
		int hoveredAttr = 0;
		if (gui::nodes::IsPinHovered(&hoveredAttr))
		{
			const DecodedPin d = decodePin(hoveredAttr);
			if (const flow::Node* n = findNode(graph, d.node))
			{
				const flow::Port* p = d.output ? n->findOutput(d.port) : n->findInput(d.port);
				if (p != nullptr)
				{
					gui::BeginTooltip();
					gui::Text("%s : %s", p->name().c_str(), std::string(p->typeName()).c_str());
					gui::TextUnformatted(("= " + p->describe()).c_str());
					gui::EndTooltip();
				}
			}
		}
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
			for (const NodeCategory& cat : nodeCatalog()) // catalog, not factory.keys() -> excludes boundary
			{
				for (const std::string& key : cat.keys)
				{
					if (gui::MenuItem(key.c_str()))
					{
						const flow::NodeId id = flow::edit::addNode(graph, ctx.app->nodeFactory().create(key));
						gui::nodes::SetNodeScreenSpacePos(static_cast<int>(id.value()), math::Vec2f{mouse.x, mouse.y});
						edited = true;
					}
				}
			}
			gui::EndPopup();
		}
		gui::End();
		m_laidOut = true;
		if (seedPositions)
			ctx.pendingLayout.clear(); // only clear after the frame that actually consumed it (not the Load frame)

		// Persistent node palette: a left-click list of the factory's node types — trackpad-native,
		// where the right-click add menu above (kept as a secondary) is awkward on a MacBook. A new node
		// cascades its grid position so successive adds don't stack.
		gui::SetNextWindowPos(math::Vec2f{20.0f, 360.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 230.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Nodes");
		for (const NodeCategory& cat : nodeCatalog()) // catalog, not factory.keys() -> excludes boundary
		{
			for (const std::string& key : cat.keys)
			{
				if (gui::Button(key.c_str()))
				{
					ctx.addCatalogNode(key);
					edited = true;
				}
			}
		}
		gui::End();
	}
} // namespace flowview
