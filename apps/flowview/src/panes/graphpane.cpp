#include "graphpane.h"

#include "../appcontext.h"
#include "../flowviewapp.h" // ctx.app->nodeFactory()
#include "../groupnav.h"	// breadcrumb / descend / editability
#include "../scene.h"		// nodeCatalog + NodeCategory
#include "canvasstate.h"	// selectedNodes

#include <lain/data/value.h>
#include <lain/flow/boundary.h> // GroupInputNode / GroupOutputNode
#include <lain/flow/dynamicports.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
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
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// The port an imnodes attribute id stands for, or nullptr — for a hovered/dragged pin, whose node
	// may have been deleted earlier this frame (and whose canvas id may predate this document).
	static const flow::Port* findPin(const flow::Graph& graph, const CanvasIds& ids, int attr, bool* isOutput = nullptr)
	{
		const auto pin = ids.toPin(attr);
		if (!pin || !graph.contains(pin->address.node))
			return nullptr;
		if (isOutput != nullptr)
			*isOutput = pin->output;
		const flow::Node& node = graph.node(pin->address.node);
		return pin->output ? node.findOutput(pin->address.port) : node.findInput(pin->address.port);
	}

	// A link was dragged between two pins: orient them (one output, one input) and ask
	// the edit layer to wire it, replacing whatever fed the input. flow::edit rejects a
	// bad drag (type mismatch, cycle) and, on rejection, leaves the existing edge intact
	// — so a bad drag onto an occupied input no longer destroys its wire. Returns
	// whether an edge was made.
	static bool tryConnect(flow::Graph& graph, const CanvasIds& ids, int startAttr, int endAttr)
	{
		const auto a = ids.toPin(startAttr);
		const auto b = ids.toPin(endAttr);
		if (!a || !b || a->output == b->output)
			return false; // need exactly one output and one input, both known to this document
		return flow::edit::connectReplacing(graph, a->output ? a->address : b->address,
											a->output ? b->address : a->address);
	}

	// The currently-selected links as edges (Delete key). A link's canvas id names the input it
	// feeds, so resolve to the live Edge values here, before any mutation — flow::edit then removes
	// by stable identity.
	static std::vector<flow::Graph::Edge> selectedEdges(const flow::Graph& graph, const CanvasIds& ids)
	{
		std::vector<flow::Graph::Edge> out;
		const int selected = gui::nodes::NumSelectedLinks();
		if (selected <= 0)
			return out;

		std::vector<int> linkIds(static_cast<std::size_t>(selected));
		gui::nodes::GetSelectedLinks(linkIds.data());
		for (const int canvasId : linkIds)
		{
			const auto destination = ids.toLink(canvasId);
			if (!destination)
				continue;
			for (const flow::Graph::Edge& edge : graph.edges())
			{
				if (edge.to == *destination)
					out.push_back(edge);
			}
		}
		return out;
	}

	// A dynamic node drawn on the CANVAS gets a small "+ <type>" per accepted port type (a
	// homogeneous Merge/Select shows one; a multi-type node one each). A click records a deferred add
	// — applied after EndNodeEditor. Takes the node const + only appends to `pinAdds`, so it is safe to
	// call mid-draw (the graph mutation happens later). PushID(node) keeps same-labelled buttons on
	// different nodes distinct.
	// Returns whether it submitted any widget — the caller needs to know, because a node whose body
	// submits NOTHING trips ImGui's "SetCursorPos used to extend boundaries" assert (imnodes positions
	// the body with SetCursorPos, and an empty group has no item to grow it).
	static bool renderCanvasAddPin(const flow::DynamicPortsNode& node, flow::NodeId id, int canvasId,
								   std::vector<std::pair<flow::NodeId, std::string>>& pinAdds)
	{
		bool drew = false;
		gui::PushID(canvasId);
		for (const std::string& key : flow::portTypeKeys())
		{
			if (!node.acceptsPortType(key))
				continue;
			drew = true;
			if (gui::SmallButton(("+ " + key).c_str()))
				pinAdds.emplace_back(id, key);
		}
		gui::PopID();
		return drew;
	}

	// Apply a saved position to node `id` if `layout` has one; returns whether it did (so the caller
	// falls back to the default column layout otherwise).
	static bool applyLayout(const flow::serialize::EditorData& layout, flow::NodeId id, int canvasId)
	{
		const auto it = layout.find(id);
		if (it == layout.end())
			return false;
		const data::Value* x = it->second.find("x");
		const data::Value* y = it->second.find("y");
		gui::nodes::SetNodeGridSpacePos(canvasId,
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

	void GraphPane::onNavigated()
	{
		// Re-seed positions and drop the selection. Both are REQUIRED, not tidiness, and unique canvas
		// ids do NOT retire them: imnodes destroys a node's data the first frame it is not submitted,
		// so descending discards the level we came from regardless of what its int was; and the
		// selection is stored as pool indices that the same pass frees without pruning.
		//
		// The window drives this at the START of a frame, before the path is resolved — never the
		// canvas itself, because a navigation can be requested DURING the canvas draw (a breadcrumb
		// click, a double-click descent). Consuming it here would seed the level being left and leave
		// the level being entered un-seeded, which is exactly that inherited-position bug.
		m_laidOut = false;
		gui::nodes::ClearNodeSelection();
		gui::nodes::ClearLinkSelection();
	}

	void GraphPane::onGraphReplaced()
	{
		m_laidOut = false; // re-seed positions from the loaded layout next frame
		gui::nodes::ClearNodeSelection();
		gui::nodes::ClearLinkSelection();
	}

	void GraphPane::draw(AppContext& ctx, const flow::Graph& graph, flow::Graph* editableGraph,
						 const flow::Evaluation& evaluation, bool& edited)
	{
		gui::SetNextWindowPos(math::Vec2f{360.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{880.0f, 600.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Graph");

		// Breadcrumb: where we are in the nesting, and the way back out. Drawn before the editor so it
		// sits above the canvas rather than floating over it.
		const flow::Graph& rootGraph = ctx.app->graph();
		// One source for "may I edit this?": having something to edit through.
		const bool editable = editableGraph != nullptr;
		{
			// The bar reads as one lineage, but its two halves cost very different things — so they get
			// different separators. "<" precedes a DOCUMENT crumb: that document is not loaded, and
			// clicking it is a swap that may prompt to save. "/" precedes a graph level WITHIN the open
			// document: already in memory, so clicking it is just a view change.
			//   parent.json < boof.json * / denoise / sharpen                              [Edit ...]
			// (ASCII "<" rather than a nicer glyph: the default ImGui font's range stops at U+00FF.)
			for (std::size_t i = 0; i < ctx.returnStack.size(); ++i)
			{
				gui::PushID(static_cast<int>(100 + i));
				if (gui::SmallButton(ctx.returnStack[i].filename().string().c_str()))
					ctx.returnRequest = i; // the menu bar owns document swaps; it consumes this
				gui::PopID();
				gui::SameLine();
				gui::TextUnformatted("<");
				gui::SameLine();
			}

			// The first graph crumb names the DOCUMENT, not "root": which file this is was otherwise
			// shown nowhere, and the dirty marker rides along with it.
			const std::vector<Crumb> crumbs = breadcrumb(rootGraph, ctx.activePath);
			const std::string document = (ctx.currentPath.empty() ? std::string("Untitled") : ctx.currentPath.filename().string()) + (ctx.dirty ? " *" : "");
			for (std::size_t i = 0; i < crumbs.size(); ++i)
			{
				if (i != 0)
				{
					gui::SameLine();
					gui::TextUnformatted("/");
					gui::SameLine();
				}
				gui::PushID(static_cast<int>(i));
				const std::string& label = (i == 0) ? document : crumbs[i].label;
				const bool here = (i + 1 == crumbs.size());
				if (here)
					gui::TextUnformatted(label.c_str()); // the level in view isn't a link
				else if (gui::SmallButton(label.c_str()))
					ctx.navigateTo(GraphPath(ctx.activePath.begin(), ctx.activePath.begin() + static_cast<std::ptrdiff_t>(crumbs[i].depth)));
				gui::PopID();
			}
			// (The read-only STATE is shown on the canvas itself — see the overlay after the editor —
			// rather than here: this strip is about where you are, not what you may do, and it has no
			// width to spare next to the Edit button.)

			// The way to change it, right-aligned on the same line: the marker states the
			// constraint, the button offers the way out of it. Named for the file, since "edit what?"
			// is not obvious several levels deep.
			if (const flow::LinkedGroupNode* linked = enclosingLinkedGroup(rootGraph, ctx.activePath))
			{
				const std::string label = "Edit " + std::filesystem::path(linked->source()).filename().string();
				const float width = gui::CalcTextSize(label.c_str()).x + gui::GetStyle().FramePadding.x * 4.0f;
				const float avail = gui::GetWindowContentRegionMax().x;
				gui::SameLine();
				if (avail - width > gui::GetCursorPosX()) // only right-align when it actually fits
					gui::SetCursorPosX(avail - width);
				if (gui::SmallButton(label.c_str()))
					ctx.editTemplateRequested = true;
				if (gui::IsItemHovered())
				{
					// The blast radius, stated before the click: editing a template changes every group
					// built from it, which is the whole reason this is a deliberate gesture.
					const int uses = countLinkedInstances(rootGraph, linked->source());
					gui::SetTooltip("Edit %s as the document.\nUsed by %d linked group(s) here.",
									linked->source().c_str(), uses);
				}
			}
		}

		const ImVec2 canvasSize = gui::GetContentRegionAvail(); // captured before the editor: to centre a located node
		const ImVec2 canvasOrigin = gui::GetCursorScreenPos();	// ... and where it starts, for the read-only overlay

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
		// This level's stored positions (a loaded document's, or those captured when it was last shown).
		static const flow::serialize::EditorData kNoLayout;
		const flow::serialize::EditorTree* levelTree = findLayoutAt(ctx.layout, ctx.activePath);
		const flow::serialize::EditorData& levelLayout = levelTree ? levelTree->nodes : kNoLayout;

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
			const int canvasId = ctx.canvas.node(id);
			// Default layout by role (a loaded layout wins over it): Input -> leftmost, Output ->
			// rightmost, others fill the columns between.
			if (seedPositions && !applyLayout(levelLayout, id, canvasId))
			{
				int col = 1 + middleColumn;
				if (dynamic_cast<const flow::GroupInputNode*>(&node) != nullptr)
					col = 0;
				else if (dynamic_cast<const flow::GroupOutputNode*>(&node) != nullptr)
					col = lastColumn;
				else
					++middleColumn;
				gui::nodes::SetNodeGridSpacePos(canvasId, math::Vec2f{60.0f + col * 240.0f, 200.0f});
			}

			// State (dim) trumps identity (category colour): an inactive node — one the run skipped
			// because a Required input was empty (ADR-0007) — is fully muted; an active one wears its
			// kind's title colour. Push counted so the pop matches whichever branch ran.
			const bool active = evaluation.ready(id); // an unready node (a Required input empty) was skipped -> dim
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
			// The title is the node's name alone: names are user-editable (Inspector ▸ Name) and the
			// palette gives each added node a unique one, so the old "[id]" disambiguator is noise here.
			// The id still shows in the Inspector header (and the cli dump) when you need it.
			const char* titleText = node.name().c_str();
			float labelColumn = gui::CalcTextSize(titleText).x;
			for (std::size_t i = 0; i < node.inputCount(); ++i)
				labelColumn = std::max(labelColumn, gui::CalcTextSize(node.input(i).name().c_str()).x);
			for (std::size_t o = 0; o < node.outputCount(); ++o)
				labelColumn = std::max(labelColumn, gui::CalcTextSize(node.output(o).name().c_str()).x);

			// LAYOUT is part of a linked group's read-only-ness. imnodes drags nodes itself, entirely
			// outside our edit gestures, so gating those never touched it — and a drag here would be
			// captured into the parent document only to be overwritten by the template's own positions
			// on the next load. Set every frame: editability changes as the user navigates.
			gui::nodes::SetNodeDraggable(canvasId, editable);

			// A node body must submit at least one item: imnodes sets the content origin with
			// SetCursorPos, and ImGui asserts on an empty group that moved the cursor past the parent
			// bounds. Most nodes have pins; a freshly added GROUP has none (its ports mirror an inner
			// boundary that starts empty), so it needs the placeholder below.
			bool bodyItems = node.inputCount() != 0 || node.outputCount() != 0;

			gui::nodes::BeginNode(canvasId);
			gui::nodes::BeginNodeTitleBar();
			gui::TextUnformatted(titleText);
			gui::nodes::EndNodeTitleBar();

			// Detach flag on inputs only (so an output drag creates a new link -> fan-out).
			gui::nodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& in = node.input(i);
				// Fill = carries a value, hollow = empty (unconnected, or upstream produced nothing) —
				// connectedness is read from the wire. Colour = type always (its label is just the name;
				// type + value are in the hover tooltip).
				const ImNodesPinShape shape = evaluation.hasValue({id, in.id()}) ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle;
				gui::nodes::PushColorStyle(ImNodesCol_Pin, gui::packColor(pinMuted(false, in.type()) ? m_style.mutedPin() : m_style.portColor(in.type(), in.typeName())));
				gui::nodes::BeginInputAttribute(ctx.canvas.pin({id, in.id()}, false), shape);
				gui::TextUnformatted(in.name().c_str());
				gui::nodes::EndInputAttribute();
				gui::nodes::PopColorStyle();
			}
			gui::nodes::PopAttributeFlag();
			// A dynamic node that grows its INPUT side gets its ± below the inputs.
			const auto* dynamic = dynamic_cast<const flow::DynamicPortsNode*>(&node);
			if (dynamic != nullptr && dynamic->dynamicSide() == flow::Port::Direction::Input)
				bodyItems |= renderCanvasAddPin(*dynamic, id, canvasId, pinAdds);
			for (std::size_t o = 0; o < node.outputCount(); ++o)
			{
				const flow::Port& out = node.output(o);
				// Fill = produced a value this run, hollow = empty (a suppressed node's outputs) — same
				// value-presence rule as inputs.
				const ImNodesPinShape shape = evaluation.hasValue({id, out.id()}) ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle;
				gui::nodes::PushColorStyle(ImNodesCol_Pin, gui::packColor(pinMuted(true, out.type()) ? m_style.mutedPin() : m_style.portColor(out.type(), out.typeName())));
				gui::nodes::BeginOutputAttribute(ctx.canvas.pin({id, out.id()}, true), shape);
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
				bodyItems |= renderCanvasAddPin(*dynamic, id, canvasId, pinAdds);

			// Nothing was submitted: give the body an item (required — see above) and, for a group,
			// say what to do about it. A group with no pins isn't broken, it is just empty: its ports
			// mirror an inner boundary the user grows after descending into it.
			if (!bodyItems)
			{
				if (node.innerGraph() != nullptr)
					gui::TextDisabled("(empty - double-click)");
				else
					gui::TextDisabled("(no pins)");
			}
			gui::nodes::EndNode();

			for (int k = 0; k < nodeColoursPushed; ++k)
				gui::nodes::PopColorStyle();
		}

		// Links carry their source pin's type colour, muted when the source produced nothing (a dead
		// edge downstream of a suppressed node). A link's canvas id comes from the input it feeds —
		// an edge needs no identity of its own, since an input takes a single source.
		for (const flow::Graph::Edge& edge : graph.edges())
		{
			const flow::Port* src = graph.node(edge.from.node).findOutput(edge.from.port);
			const bool edgeActive = (src != nullptr) && evaluation.hasValue(edge.from);
			gui::nodes::PushColorStyle(ImNodesCol_Link, gui::packColor(edgeActive ? m_style.portColor(src->type(), src->typeName()) : m_style.mutedLink()));
			gui::nodes::Link(ctx.canvas.link(edge.to), ctx.canvas.pin(edge.from, true), ctx.canvas.pin(edge.to, false));
			gui::nodes::PopColorStyle();
		}

		// A minimap (bottom-right) — an overview + click-to-navigate, so nodes panned off-screen aren't
		// lost (imnodes has no zoom; a minimap + panning is the mitigation until an imgui-node-editor
		// migration would add zoom).
		gui::nodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
		gui::nodes::EndNodeEditor();

		// Read-only watermark, bottom-left of the canvas: the state belongs where the gestures happen,
		// not in the breadcrumb. Muted, so it reads as chrome rather than as an error — the transient
		// message below is what speaks up when someone actually tries to edit. Deliberately NOT a
		// background tint: canvasstyle already uses dimming to mean "this node did not run".
		if (!editable)
		{
			// Scaled well above body text: at the default 13px in a background tone this was invisible.
			// The AddText overload taking an explicit font size is what allows that without a second
			// font — the default atlas is a bitmap, so ~1.8x is about as far as it stays crisp.
			const char* mark = "LINKED - READ ONLY";
			const float size = gui::GetFontSize() * 1.8f;
			const math::Vec2f at{canvasOrigin.x + 12.0f, canvasOrigin.y + canvasSize.y - size - 10.0f};
			gui::GetWindowDrawList()->AddText(gui::GetFont(), size, at, gui::packColor(m_style.readOnlyMark()), mark);
		}

		// Centre a located node (from an Issue click): pan so it sits at the canvas centre. Done here —
		// the node's grid position + size are only known once it has been drawn.
		if (ctx.locateTarget)
		{
			const int nid = ctx.canvas.node(*ctx.locateTarget);
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

		// Double-click a group to go inside it. The SAME gesture for both kinds — a linked group is
		// navigable and inspectable, it just can't be edited in place — so there is one way in, not
		// two. Its live intermediates are already computed (the run's plan covers every level), so
		// looking inside costs nothing.
		int hoveredNode = 0;
		const bool overNode = canvasActive && gui::nodes::IsNodeHovered(&hoveredNode);
		if (overNode && gui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			const auto target = ctx.canvas.toNode(hoveredNode);
			if (target && graph.contains(*target) && graph.node(*target).innerGraph() != nullptr)
				ctx.descendInto(*target);
		}

		// A drag that SetNodeDraggable just refused. Alt+drag is the canvas pan (see init), so it is
		// excluded — panning is a view action and stays available in a read-only graph.
		if (!editable && overNode && gui::IsMouseDragging(ImGuiMouseButton_Left) && !gui::GetIO().KeyAlt)
			ctx.noteReadOnlyEdit();

		// Below a linked group the recipe belongs to its template, so every MUTATING gesture is off
		// (the breadcrumb says why, and the menu bar offers "Edit Template..." to change it
		// deliberately). Gated per gesture rather than as one block, so the read-only affordances —
		// hovering a pin for its value, following links — keep working: looking inside a linked group
		// is the whole point of letting you navigate into it.
		// Apply the canvas ± requests collected during the node draw (auto-named `<prefix><count>` on
		// the growing side), now that the graph can be mutated safely.
		if (!editable && !pinAdds.empty())
			ctx.noteReadOnlyEdit();
		for (const auto& [nodeId, key] : (editable ? pinAdds : std::vector<std::pair<flow::NodeId, std::string>>{}))
		{
			const flow::DynamicPortsNode& dyn = static_cast<const flow::DynamicPortsNode&>(graph.node(nodeId));
			const bool onOutput = dyn.dynamicSide() == flow::Port::Direction::Output;
			const std::size_t count = onOutput ? dyn.outputCount() : dyn.inputCount();
			const std::string name = std::string(onOutput ? "out" : "in") + std::to_string(count);
			if (flow::edit::addPort(*editableGraph, nodeId, key, name) != flow::PortId{})
				edited = true;
		}
		// A detached link (dropped in empty space, or the "off" side of a move). Handle
		// it before IsLinkCreated so a move frees the input before the reattach lands.
		int destroyedLink = 0;
		// Query imnodes regardless of editability — it holds this state until asked, and a stale answer
		// would surface on the next graph that IS editable.
		if (gui::nodes::IsLinkDestroyed(&destroyedLink))
		{
			const auto destination = ctx.canvas.toLink(destroyedLink);
			if (!destination)
			{
				// Not ours (or from a document since closed) — nothing to disconnect.
			}
			else if (editable)
			{
				edited |= flow::edit::disconnect(*editableGraph, *destination);
			}
			else
			{
				ctx.noteReadOnlyEdit();
			}
		}
		int startAttr = 0;
		int endAttr = 0;
		if (gui::nodes::IsLinkCreated(&startAttr, &endAttr))
		{
			if (!editable)
				ctx.noteReadOnlyEdit();
			else if (tryConnect(*editableGraph, ctx.canvas, startAttr, endAttr))
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
			bool fromOutput = false;
			if (const flow::Port* p = findPin(graph, ctx.canvas, dragAttr, &fromOutput))
			{
				m_linkDragActive = true;
				m_linkDragFromOutput = fromOutput;
				m_linkDragType = p->type();
			}
		}
		if (m_linkDragActive && !gui::IsMouseDown(ImGuiMouseButton_Left))
			m_linkDragActive = false; // mouse released -> the drag is over (connected, dropped, or cancelled)

		// Pin tooltip: hovering a pin shows its full type + current value (the terse canvas label is
		// just the name). Its node may have been deleted this frame, so look it up guarded.
		int hoveredAttr = 0;
		if (gui::nodes::IsPinHovered(&hoveredAttr))
		{
			// The declaration says name and type; the evaluation says what is actually on it.
			const auto pin = ctx.canvas.toPin(hoveredAttr);
			if (const flow::Port* p = findPin(graph, ctx.canvas, hoveredAttr))
			{
				gui::BeginTooltip();
				gui::Text("%s : %s", p->name().c_str(), std::string(p->typeName()).c_str());
				gui::TextUnformatted(("= " + evaluation.describe(pin->address)).c_str());
				gui::EndTooltip();
			}
		}
		if (!editable && canvasActive && gui::IsKeyPressed(ImGuiKey_Delete) && !selectedNodes(ctx.canvas).empty())
			ctx.noteReadOnlyEdit();
		if (editable && canvasActive && gui::IsKeyPressed(ImGuiKey_Delete))
		{
			// Resolve the selection to stable values first, then delete in one edit.
			const std::vector<flow::Graph::Edge> edgesToRemove = selectedEdges(graph, ctx.canvas);
			const std::vector<flow::NodeId> nodesToRemove = selectedNodes(ctx.canvas);
			if (flow::edit::remove(*editableGraph, nodesToRemove, edgesToRemove))
			{
				// Drop imnodes' now-stale selection. Its ids resolve to nothing once the objects are
				// gone (they are never recycled), but imnodes stores a selection as POOL INDICES,
				// which it frees without pruning — so a leftover entry can later name another node.
				gui::nodes::ClearLinkSelection();
				gui::nodes::ClearNodeSelection();
				edited = true;
			}
		}
		// Right-click empty canvas -> add-node palette (the imnodes color_node_editor
		// pattern: focus + editor hover + mouse release).
		if (canvasActive && gui::nodes::IsEditorHovered() && gui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			if (editable)
				gui::OpenPopup("addNode");
			else
				ctx.noteReadOnlyEdit(); // the add menu would have nowhere to put a node
		}
		// `editable` short-circuits, so the palette is not even submitted without a graph to add to —
		// ImGui closes an open popup whose Begin stops being called, which is the right outcome if the
		// user navigates into a linked group while it is up.
		if (editable && gui::BeginPopup("addNode"))
		{
			const ImVec2 mouse = gui::GetMousePosOnOpeningCurrentPopup();
			for (const NodeCategory& cat : nodeCatalog()) // catalog, not factory.keys() -> excludes boundary
			{
				for (const std::string& key : cat.keys)
				{
					if (gui::MenuItem(key.c_str()))
					{
						const flow::NodeId id = flow::edit::addNode(*editableGraph, ctx.app->nodeFactory().create(key));
						gui::nodes::SetNodeScreenSpacePos(ctx.canvas.node(id), math::Vec2f{mouse.x, mouse.y});
						edited = true;
					}
				}
			}
			gui::EndPopup();
		}
		gui::End();
		m_laidOut = true;

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
				// addCatalogNode refuses inside a linked group and lands in the ACTIVE graph, so the
				// palette needs no rule of its own — it just respects the answer.
				if (gui::Button(key.c_str()))
					edited |= ctx.addCatalogNode(key) != flow::NodeId{};
			}
		}
		gui::End();
	}
} // namespace flowview
