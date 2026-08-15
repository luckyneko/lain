#include "inspectorpane.h"

#include "../appcontext.h"
#include "../flowviewapp.h" // ctx.app->reevaluate()
#include "../groupnav.h"	// editableAt — a linked group's nodes belong to its template
#include "../parameditors.h"
#include "../previewcache.h"
#include "canvasstate.h" // selectedNodes
#include "imagesave.h"	 // renderImageSave

#include <lain/flow/boundary.h> // GroupInputNode / GroupOutputNode (dynamic_cast)
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/param.h>
#include <lain/flow/port.h>
#include <lain/flow/portvalue.h>
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/math/types.h>
#include <lain/string/format.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	void InspectorPane::draw(AppContext& ctx, const flow::Graph& graph, flow::Graph* editableGraph,
							 flow::Evaluation& evaluation, PreviewCache& previews, const ParamEditors& editors)
	{
		// Whether the graph on screen may be edited at all: false inside a linked group, and expressed
		// by there being nothing to edit through rather than by a separate check.
		const bool editable = editableGraph != nullptr;
		bool paramEdited = false;
		bool renamed = false; // a title edit: a document change, but no recompute (see below)

		gui::SetNextWindowPos(math::Vec2f{20.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 320.0f}, ImGuiCond_FirstUseEver);
		if (gui::Begin("Inspector"))
		{

			// Selection-driven: inspect only the node(s) selected on the canvas (stacked, walked in topo
			// order for a stable top-to-bottom layout), not the whole graph. Nothing selected -> a hint.
			// (The Interface panel is the separate host-binding surface.)
			const std::vector<flow::NodeId> selection = selectedNodes(ctx.canvas);
			if (selection.empty())
				gui::TextDisabled("Select a node on the canvas to inspect it.");

			for (const flow::NodeId id : graph.topoOrder())
			{
				if (std::find(selection.begin(), selection.end(), id) == selection.end())
					continue; // only the selected nodes

				// Read through the const graph; WRITE through the editable one, which is null inside a
				// linked group — so a param or name edit there has nowhere to land by construction.
				const flow::Node& node = graph.node(id);
				flow::Node* writable = editable ? &editableGraph->node(id) : nullptr;
				// An obvious titled section per selected node. The id stays in the Inspector header (the
				// detail surface — it's what the cli dump and any log line names) even though the canvas
				// title shows the user's name alone. Truncated: a full uuid would swamp the header, and
				// the tail is enough to tell two nodes apart or match a dump line.
				const std::string header = string::format("{} [{}]", node.name(), id.shortString());
				gui::SeparatorText(header.c_str());

				// Boundary nodes are edited in the Interface panel (their whole-graph I/O view), not here —
				// no point duplicating their pins in the per-node Inspector.
				if (dynamic_cast<const flow::GroupInputNode*>(&node) != nullptr || dynamic_cast<const flow::GroupOutputNode*>(&node) != nullptr)
				{
					gui::TextDisabled("Boundary node - edit its pins in the Interface panel.");
					continue;
				}

				// Editable params, chosen by type via the registry (file field, drags, colour
				// swatch). PushID(node) so same-named params on different nodes don't collide.
				gui::PushID(ctx.canvas.node(id));

				// Inside a LINKED group, a node's name and params belong to the template: the parent
				// document stores only the source path + cached interface, so an edit here would be
				// carried into the group and then silently lost on save. Disabled, not hidden — reading
				// a template's params is exactly why you are allowed to look inside one.
				gui::BeginDisabled(!editable);

				// The node's title: user-editable, defaulting to the name its type gave it. Committed on
				// Enter / focus loss (the pin-rename idiom) and only when it actually changed, so holding
				// focus doesn't churn. Display only — nothing addresses a node by name — so it needs no
				// re-run, just the unsaved-changes mark (the name is serialized with the graph). A blank
				// entry is refused: the field reverts to the current name on the next frame.
				std::string nodeName = node.name();
				gui::InputText("Name", &nodeName); // (its per-keystroke return is not the commit signal)
				if (writable != nullptr && gui::IsItemDeactivatedAfterEdit() && !nodeName.empty() && nodeName != node.name())
				{
					writable->setName(std::move(nodeName));
					renamed = true;
				}

				// The params that back a DEFAULTED INPUT, and whether that input is currently wired.
				// A default is editable only while nothing drives it — the value is what the pin
				// carries when disconnected, so offering an editor for it beside a live wire would
				// invite editing a number that has no effect.
				std::map<flow::PortId, bool> drivenDefault;
				for (std::size_t i = 0; i < node.inputCount(); ++i)
				{
					const flow::Port& in = node.input(i);
					const flow::Param* fallback = node.defaultOf(in.id());
					if (fallback == nullptr)
						continue;
					const flow::PortAddress address{id, in.id()};
					bool wired = false;
					for (const flow::Graph::Edge& e : graph.edges())
					{
						if (e.to == address)
							wired = true;
					}
					drivenDefault[fallback->id()] = wired;
				}

				bool nodeEdited = false;
				for (std::size_t pi = 0; pi < node.paramCount(); ++pi)
				{
					// The editor works on a DETACHED copy of the value, which is then committed
					// through the node. A PortValue copy is a refcount bump (the payload is shared
					// and immutable), and `set` rebinds rather than writing through, so an
					// in-progress edit cannot disturb the live param — only a successful commit
					// does. setParam invalidates as part of the write, so there is no markDirty to
					// forget here.
					const flow::Param& p = node.param(pi);
					const flow::PortId paramId = p.id();
					const auto driven = drivenDefault.find(paramId);
					if (driven != drivenDefault.end() && driven->second)
					{
						// Driven by a wire: say so where the editor would have been, so the value's
						// absence reads as "the graph supplies this" rather than as a missing widget.
						gui::TextDisabled("%s: driven by input", p.name().c_str());
						continue;
					}
					flow::PortValue edited = p.value();
					if (editors.render(p.name(), p.type(), edited) && writable != nullptr)
						nodeEdited |= writable->setParam(paramId, std::move(edited));
				}
				paramEdited |= nodeEdited;
				gui::EndDisabled();
				gui::PopID();

				auto port = [&](const char* tag, const flow::Port& p, bool output)
				{
					// An image port carrying a value shows extent + its thumbnail (uploaded + cached
					// by the preview cache, keyed by the port's stable id); everything else — CPU
					// values and the empty slot — is text via the shared describe() pathway. The
					// value comes from the EVALUATION; the port only declares its type.
					const flow::PortValue& value = evaluation.value(flow::PortAddress{id, p.id()});
					if (!value.empty() && p.type() == typeid(image::Image))
					{
						const image::Image& img = value.get<image::Image>();
						gui::Text("    %s %s: %s %dx%d", tag, p.name().c_str(), std::string(p.typeName()).c_str(), img.width(), img.height());
						const PinKey key{ctx.activePath, flow::PortAddress{id, p.id()}};
						if (const gui::Texture* tex = previews.find(key))
						{
							// Fits the pane's width, capped in height, aspect preserved — no size
							// setting: the Preview pane is where you go for a proper look.
							gui::Image(*tex, previewFit(img.extent(), thumbnailBox())); // Texture -> ImTextureRef implicitly
							if (gui::IsItemClicked())
								ctx.previewAsset(key); // click a thumbnail -> full-size in the Preview pane
						}
						if (output) // outputs are the results you'd export; inputs are just what was fed in
						{
							gui::PushID(ctx.canvas.pin({id, p.id()}, output));
							renderImageSave(ctx, key, img);
							gui::PopID();
						}
						return;
					}
					gui::Text("    %s %s: %s", tag, p.name().c_str(), evaluation.describe(flow::PortAddress{id, p.id()}).c_str());
				};

				for (std::size_t i = 0; i < node.inputCount(); ++i)
					port("in ", node.input(i), false);
				for (std::size_t i = 0; i < node.outputCount(); ++i)
					port("out", node.output(i), true);
			}
		}
		gui::End();

		// A param edit re-runs the scene (a full run recomputes every node) and marks the
		// previews for refresh next frame — the same path a canvas edit takes.
		if (paramEdited)
		{
			ctx.app->reevaluate();
			previews.markDirty();
			ctx.markChanged(); // a param edit -> unsaved changes + an undo snapshot
			ctx.loadIssues.clear();
		}
		// A rename changes no value, so it needs neither a re-run nor a preview refresh — but it IS part
		// of the saved document, so it must mark the change (guard + undo), same as a boundary-pin rename.
		if (renamed)
		{
			ctx.markChanged();
			ctx.loadIssues.clear();
		}
	}
} // namespace flowview
