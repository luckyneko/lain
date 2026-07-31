#include "inspectorpane.h"

#include "../appcontext.h"
#include "../flowviewapp.h" // ctx.app->reevaluate()
#include "../groupnav.h"	// editableAt — a linked group's nodes belong to its template
#include "../parameditors.h"
#include "../previewcache.h"
#include "canvasids.h" // selectedNodes + pinId
#include "imagesave.h" // renderImageSave

#include <lain/flow/boundary.h> // GroupInputNode / GroupOutputNode (dynamic_cast)
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/param.h>
#include <lain/flow/port.h>
#include <lain/gui/enums.h> // enumCombo
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/math/types.h>
#include <lain/string/format.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	void InspectorPane::draw(AppContext& ctx, flow::Graph& graph, PreviewCache& previews, const ParamEditors& editors)
	{
		// Whether the graph on screen may be edited at all (false inside a linked group — see below).
		const bool editable = editableAt(ctx.app->graph(), ctx.activePath);
		bool paramEdited = false;
		bool renamed = false; // a title edit: a document change, but no recompute (see below)

		gui::SetNextWindowPos(math::Vec2f{20.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 320.0f}, ImGuiCond_FirstUseEver);
		if (gui::Begin("Inspector"))
		{
			gui::enumCombo("Preview size", ctx.previewSize); // labels from lain::meta::enums

			// Selection-driven: inspect only the node(s) selected on the canvas (stacked, walked in topo
			// order for a stable top-to-bottom layout), not the whole graph. Nothing selected -> a hint.
			// (The Interface panel is the separate host-binding surface.)
			const std::vector<flow::NodeId> selection = selectedNodes();
			if (selection.empty())
				gui::TextDisabled("Select a node on the canvas to inspect it.");

			for (const flow::NodeId id : graph.topoOrder())
			{
				if (std::find(selection.begin(), selection.end(), id) == selection.end())
					continue; // only the selected nodes

				flow::Node& node = graph.node(id); // non-const: the title + params are edited below
				// An obvious titled section per selected node. The id stays in the Inspector header (the
				// detail surface — it's what the cli dump and any log line names) even though the canvas
				// title now shows the user's name alone.
				const std::string header = string::format("{} [{}]", node.name(), id.value());
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
				gui::PushID(static_cast<int>(id.value()));

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
				if (gui::IsItemDeactivatedAfterEdit() && !nodeName.empty() && nodeName != node.name())
				{
					node.setName(std::move(nodeName));
					renamed = true;
				}

				bool nodeEdited = false;
				for (flow::PortIndex pi = 0; pi < node.paramCount(); ++pi)
				{
					flow::Param& p = node.param(pi);
					nodeEdited |= editors.render(p.name(), p.type(), p.value());
				}
				if (nodeEdited)
					node.markDirty(); // a param edit -> incremental re-eval recomputes this node + downstream
				paramEdited |= nodeEdited;
				gui::EndDisabled();
				gui::PopID();

				auto port = [&](const char* tag, const flow::Port& p, bool output)
				{
					// A ready image port shows extent + its thumbnail (uploaded + cached by the
					// preview cache, keyed by the port's stable id); everything else — CPU values
					// and the empty slot — is text via the shared Port::describe() pathway.
					if (p.ready() && p.type() == typeid(image::Image))
					{
						const image::Image& img = p.value().get<image::Image>();
						gui::Text("    %s %s: %s %dx%d", tag, p.name().c_str(), std::string(p.typeName()).c_str(), img.width(), img.height());
						const PinKey key{id.value(), output, p.id()};
						if (const gui::Texture* tex = previews.find(key))
						{
							const float side = previewExtent(ctx.previewSize);
							gui::Image(*tex, math::Vec2f{side, side}); // Texture -> ImTextureRef implicitly
							if (gui::IsItemClicked())
								ctx.previewAsset(key); // click a thumbnail -> full-size in the Preview pane
						}
						if (output) // outputs are the results you'd export; inputs are just what was fed in
						{
							gui::PushID(pinId(id, output, p.id()));
							renderImageSave(ctx, key, img);
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
