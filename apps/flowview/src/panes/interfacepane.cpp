#include "interfacepane.h"

#include "../appcontext.h"
#include "../flowviewapp.h" // ctx.app->reevaluate()
#include "../parameditors.h"
#include "../previewcache.h"
#include "imagesave.h"

#include <lain/flow/boundary.h> // GroupInputNode / GroupOutputNode
#include <lain/flow/dynamicports.h>
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/flow/porttyperegistry.h> // portTypeKeys
#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/math/types.h>

#include <string>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

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
	// Returns whether the pin was actually renamed.
	//
	// A new name must be a valid port name AND unused on this side of the node: serialization
	// addresses edges by port name, so two pins sharing one would make an edge ambiguous on load
	// (addDynamicPort enforces the same rule at creation — a rename must not be the way around it).
	// A refused name is simply not applied, and the field reverts to the current one next frame.
	static bool renderPinName(const flow::Node& node, flow::Port& pin)
	{
		std::string name = pin.name();
		gui::SetNextItemWidth(110.0f);
		gui::InputText("##name", &name);
		if (!gui::IsItemDeactivatedAfterEdit() || name == pin.name())
			return false;
		if (!flow::validPortName(name) || node.hasPortNamed(pin.direction(), name))
			return false;
		pin.setName(std::move(name));
		return true;
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

	bool InterfacePane::renderRemoveConfirm(flow::Graph& graph)
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

	void InterfacePane::draw(AppContext& ctx, flow::Graph& graph, PreviewCache& previews, const ParamEditors& editors)
	{
		const float side = previewExtent(ctx.previewSize);
		bool changed = false;
		bool renamed = false; // a pin rename: a document change, but no recompute (see below)

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
				renamed |= renderPinName(*node, pin);
				gui::SameLine();
				gui::Text(": %s", std::string(pin.typeName()).c_str());

				const PinKey key{node->id().value(), true, pin.id()};
				if (const gui::Texture* tex = previews.find(key))
				{
					gui::Image(*tex, math::Vec2f{side, side});
					if (gui::IsItemClicked())
						ctx.previewAsset(key); // click a thumbnail -> full-size in the Preview pane
				}

				if (pin.type() == typeid(image::Image))
				{
					if (gui::Button("Bind file..."))
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
				}
				else
				{
					// A scalar boundary input: edit its value by type via the shared registry. Read the
					// currently-published value (pin.value() == the bound value after a run), edit a copy,
					// and setValue on change (which updates the bound slot + marks the node dirty).
					flow::PortValue current = pin.value();
					if (editors.render("##value", pin.type(), current))
					{
						node->setValue(pin.id(), std::move(current));
						changed = true;
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
				renamed |= renderPinName(*node, pin);
				gui::SameLine();
				gui::Text(": %s", std::string(pin.typeName()).c_str());

				const PinKey key{node->id().value(), false, pin.id()};
				if (pin.value().empty())
				{
					// The producer was gated off / suppressed (conditional eval) — no value this run.
					// the preview cache already dropped any stale thumbnail; say so rather than show blank.
					gui::TextDisabled("(no output this run)");
				}
				else
				{
					if (const gui::Texture* tex = previews.find(key))
					{
						gui::Image(*tex, math::Vec2f{side, side});
						if (gui::IsItemClicked())
							ctx.previewAsset(key); // click a thumbnail -> full-size in the Preview pane
					}
					if (pin.value().holds<image::Image>())
						renderImageSave(ctx, key, pin.value().get<image::Image>());
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

		// An add / remove / bind re-runs the graph (as a param/canvas edit does) and refreshes previews
		// next frame. markChanged arms the guard AND requests an undo snapshot — but a bind changes only
		// the (non-serialized) bound value, so its snapshot equals the current one and the undo stack
		// ignores it; a pin add/remove does change the document and records a step. Either way New is
		// guarded — a bound image is loaded data worth not losing silently.
		if (changed)
		{
			ctx.app->reevaluate();
			previews.markDirty();
			ctx.markChanged();
			ctx.loadIssues.clear(); // load issues are stale once the graph changes
		}
		// A pin rename changes no value, so it needs neither a re-run nor a preview refresh — but the
		// name IS part of the saved document (edges are addressed by it), so mark it (guard + undo).
		if (renamed)
		{
			ctx.markChanged();
			ctx.loadIssues.clear();
		}
	}
} // namespace flowview
