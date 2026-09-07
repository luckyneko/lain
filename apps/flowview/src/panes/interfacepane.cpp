#include "interfacepane.h"

#include "../appcontext.h"
#include "../flowviewapp.h" // ctx.app->reevaluate()
#include "../groupnav.h"	// loopAt / editableLoopAt — a loop's carries live one level up
#include "../imagecanvas.h" // previewFit / thumbnailBox (the shared thumbnail sizing)
#include "../parameditors.h"
#include "../previewcache.h"
#include "imagesave.h"

#include <lain/flow/boundary.h> // GroupInputNode / GroupOutputNode
#include <lain/flow/dynamicports.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h> // LoopNode — the carry pairing this panel states and authors
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/flow/porttyperegistry.h> // portTypeKeys
#include <lain/gui/gui.h>
#include <lain/image/image.h>
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
	// The rename goes through flow::Node::renamePort, which is the seam rather than Port::setName
	// because a rename has to move every copy of the name: it refuses one that is invalid or already
	// taken on this side (serialization addresses edges by port name, so two pins sharing one makes
	// an edge ambiguous on load), and it carries the param behind a DEFAULTED pin along with the
	// port — a param is addressed by name on disk too, so leaving it behind would make the default
	// silently revert on the next load. A refused name is simply not applied, and the field reverts
	// to the current one next frame.
	//
	// Drawn from the READ side and committed through the WRITE side, which is null inside a linked
	// group — so the name is always legible there, and never editable.
	static bool renderPinName(const flow::Port& pin, flow::Node* writable)
	{
		std::string name = pin.name();
		gui::SetNextItemWidth(110.0f);
		gui::InputText("##name", &name);
		if (writable == nullptr || !gui::IsItemDeactivatedAfterEdit() || name == pin.name())
			return false;
		return writable->renamePort(pin.id(), std::move(name));
	}

	// The per-node "+" : a menu of the registered port types the node accepts (filtered by
	// acceptsPortType); picking one adds a pin via the edit seam, auto-named `<prefix><count>`.
	static bool renderAddPin(flow::Graph* editable, const flow::DynamicPortsNode& node, const char* prefix)
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
					if (editable != nullptr)
					{
						flow::edit::addPort(*editable, node.id(), key, std::string(prefix) + std::to_string(count));
						added = true;
					}
				}
			}
			if (!any)
				gui::TextDisabled("(no registered types)");
			gui::EndPopup();
		}
		return added;
	}

	// The first `carry`, `carry2`, … free on BOTH inner boundary nodes. A port name is an identifier
	// (validPortName), so it uniquifies with a digit rather than with the " 2" the canvas gives a
	// node title — and it must be free on both sides, because one gesture names both.
	static std::string freeCarryName(const flow::GroupInputNode& into, const flow::GroupOutputNode& from)
	{
		for (int n = 1;; ++n)
		{
			const std::string name = n == 1 ? std::string("carry") : "carry" + std::to_string(n);
			if (!into.hasPortNamed(flow::Port::Direction::Output, name) && !from.hasPortNamed(flow::Port::Direction::Input, name))
				return name;
		}
	}

	// The per-loop "+ add carry": the registered port types both inner boundary nodes accept, and
	// picking one adds a pin of that type to EACH side and records the pairing.
	//
	// ONE gesture, because half a carry is not a weaker carry — it is a silent invariant (or a
	// last-iteration output) nobody asked for (ADR-0021). The keyed LoopNode::addCarry is the same
	// routine the templated one runs, so what this menu authors and what a document restores cannot
	// come to mean different things.
	static bool renderAddCarry(flow::LoopNode* loop, const flow::GroupInputNode& into, const flow::GroupOutputNode& from)
	{
		bool added = false;
		if (gui::Button("+ add carry"))
			gui::OpenPopup("addCarry");
		if (gui::BeginPopup("addCarry"))
		{
			bool any = false;
			for (const std::string& key : flow::portTypeKeys())
			{
				if (!into.acceptsPortType(key) || !from.acceptsPortType(key))
					continue;
				any = true;
				if (gui::MenuItem(key.c_str()) && loop != nullptr)
					added |= loop->addCarry(key, freeCarryName(into, from)).innerIn != flow::PortId{};
			}
			if (!any)
				gui::TextDisabled("(no registered types)");
			gui::EndPopup();
		}
		return added;
	}

	bool InterfacePane::renderRemoveConfirm(flow::Graph* editable)
	{
		if (m_removeRequested)
		{
			gui::OpenPopup("Remove pin");
			m_removeRequested = false;
		}
		bool removed = false;
		if (gui::BeginPopupModal("Remove pin", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			gui::Text("Remove %s?  It has %d link(s).", m_removeLabel.c_str(), m_removeLinks);
			if (gui::Button("Remove"))
			{
				// Every target, not the first one that works: a carry is two pins, and leaving one
				// behind would turn it into a silent invariant (or a last-iteration output) the user
				// did not ask for — which is the shape the paired gesture exists to prevent.
				for (const flow::PortAddress& target : m_removeTargets)
					removed |= editable != nullptr && flow::edit::removePort(*editable, target);
				gui::CloseCurrentPopup();
			}
			gui::SameLine();
			if (gui::Button("Cancel"))
				gui::CloseCurrentPopup();
			gui::EndPopup();
		}
		return removed;
	}

	void InterfacePane::draw(AppContext& ctx, const flow::Graph& graph, flow::Graph* editableGraph,
							 flow::Evaluation& evaluation, PreviewCache& previews, const ParamEditors& editors)
	{
		bool changed = false;
		bool renamed = false; // a pin rename: a document change, but no recompute (see below)

		// Request the remove-confirm for `targets` (opened after the panel, clean id stack).
		const auto requestRemove = [&](std::string label, std::vector<flow::PortAddress> targets)
		{
			m_removeLinks = 0;
			for (const flow::PortAddress& target : targets)
				m_removeLinks += linkCount(graph, target);
			m_removeTargets = std::move(targets);
			m_removeLabel = std::move(label);
			m_removeRequested = true;
		};
		const auto requestRemovePin = [&](flow::NodeId node, const flow::Port& pin)
		{
			requestRemove("pin '" + pin.name() + "'", {flow::PortAddress{node, pin.id()}});
		};

		// The LOOP this panel is inside, if any — resolved one level up, because the pairing is the
		// loop node's and the pins are its interior's. Read through the const graph and written
		// through the mutable one, the same split renderPinName already draws with: inside a linked
		// group the carries are legible and not editable.
		const flow::LoopNode* loop = loopAt(ctx.app->graph(), ctx.activePath);
		flow::LoopNode* writableLoop = editableLoopAt(ctx.app->graph(), ctx.activePath);

		// Whether an inner boundary pin is half of a carry — the marker each list draws. Keyed by
		// PortId, never by name, because that is what the pairing IS (a rename must move a label,
		// never behaviour).
		const auto carriedIn = [&](flow::PortId pin)
		{ return loop != nullptr && loop->carries().count(pin) != 0; };
		const auto carriedOut = [&](flow::PortId pin)
		{
			if (loop == nullptr)
				return false;
			for (const auto& [innerIn, innerOut] : loop->carries())
			{
				if (innerOut == pin)
					return true;
			}
			return false;
		};

		// What a pin's row says about itself beyond its type. A RESERVED pin (a loop's `index` /
		// `continue`) is static, which is also what makes it unremovable below — one fact, read
		// twice, rather than two that can disagree.
		const auto pinNote = [&](const flow::Port& pin, bool carried) -> const char*
		{
			if (!pin.isDynamic())
				return "(reserved)";
			return carried ? "(carried)" : nullptr;
		};

		gui::SetNextWindowPos(math::Vec2f{940.0f, 20.0f}, ImGuiCond_FirstUseEver);
		gui::SetNextWindowSize(math::Vec2f{320.0f, 520.0f}, ImGuiCond_FirstUseEver);
		gui::Begin("Interface");

		// At the root this is the host-binding surface; inside a group it is the group's own port
		// list — same pins, same ± and rename, but nothing to bind.
		const bool atRoot = ctx.activePath.empty();
		// Inside a LINKED group these pins belong to the template, so shaping them here would be an
		// edit the parent document cannot store (it keeps only the source path + the cached interface)
		// — it would be carried to the group's face by the per-frame sync and then silently lost on
		// save. Disabled rather than hidden: you can still read the interface, which is the point of
		// being able to look inside at all.
		const bool editable = editableGraph != nullptr;
		if (!atRoot)
			gui::TextUnformatted(editable ? "Group interface (this group's ports)"
										  : "Group interface (linked - read only)");

		// The per-pin "×". STATIC pins are not the user's to remove: every pin a user added here is
		// dynamic (addBoundary -> addDynamicPort -> markDynamic), so a static one exists only
		// because the node that OWNS this graph declared it — today a loop's reserved `index` /
		// `continue` (ADR-0021). Removing one left LoopNode holding a PortId that names nothing:
		// the condition fell back to its default and the while loop ran to its bound with no error
		// anywhere, while a save-and-reload quietly healed it (establishReserved remakes them),
		// which is what would have made it hard to find.
		//
		// The rule is asked of the PIN, not of the level, so this panel needs no idea what a loop
		// is — and at the root, where every pin is dynamic, it changes nothing. The NAME stays
		// editable, because a reserved pin is renameable by design and its name is stored precisely
		// so that a rename survives a round trip.
		const auto removePinButton = [&](flow::NodeId node, const flow::Port& pin)
		{
			gui::BeginDisabled(!editable || !pin.isDynamic());
			if (gui::Button("x"))
				requestRemovePin(node, pin);
			gui::EndDisabled();
			if (!pin.isDynamic() && gui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				gui::SetTooltip("This pin belongs to the node that owns this graph - it cannot be removed here.");
		};

		// Inputs: per GroupInput node, each pin (editable name, bound thumbnail, Bind…, ×) + a "+".
		gui::TextUnformatted("Inputs");
		gui::Separator();
		{
			// No null check: the boundary pair is a Graph invariant. The block scopes the ID push.
			const flow::GroupInputNode& node = graph.boundaryInputNode();
			flow::GroupInputNode* const writable = editable ? &editableGraph->boundaryInputNode() : nullptr;
			gui::PushID(ctx.canvas.node(node.id()));
			for (std::size_t i = 0; i < node.outputCount(); ++i)
			{
				const flow::Port& pin = node.output(i);
				gui::PushID(static_cast<int>(pin.id().value()));
				gui::BeginDisabled(!editable);
				renamed |= renderPinName(pin, writable);
				gui::EndDisabled();
				gui::SameLine();
				gui::Text(": %s", std::string(pin.typeName()).c_str());
				if (const char* note = pinNote(pin, carriedIn(pin.id())))
				{
					gui::SameLine();
					gui::TextDisabled("%s", note);
				}

				const flow::PortAddress address{node.id(), pin.id()};
				const PinKey key{ctx.activePath, address};
				const flow::PortValue& bound = evaluation.value(address);
				if (const gui::Texture* tex = previews.find(key))
				{
					// The aspect comes from the TEXTURE: a bound value need not be an image (a
					// sequence's thumbnail is a decoded poster frame), so there may be no extent on
					// the value to ask for.
					gui::Image(*tex, previewFit(tex->extent(), thumbnailBox()));
					if (gui::IsItemClicked())
						ctx.previewAsset(key); // click a thumbnail -> full-size in the Preview pane
				}

				// Host binding is ROOT-ONLY. Inside a group this pin's value is driven by the parent's
				// edges (the plan's entry step overwrites it every run), so a bound value would be
				// silently discarded. Below the root this panel edits the INTERFACE — names and ± pins,
				// which is how a group's own ports are shaped — and does not bind it.
				//
				// EVERY type binds through the same registry: an image opens a file picker, a frame
				// sequence opens footage, a scalar edits in place. That the picker used to be an
				// `if (type == image)` here and the scalars an else was the pane deciding by type what
				// ADR-0005 says the type decides for itself — and it is why a FrameSequence boundary
				// input read "(no editor)" and could not be bound at all. Read the currently bound
				// value from the EVALUATION, edit a detached copy, and bind on change (which stores it
				// and requests the boundary node's recompute).
				if (atRoot)
				{
					flow::PortValue current = bound;
					if (editors.render("##value", pin.type(), current))
					{
						evaluation.bind(address, std::move(current));
						changed = true;
					}
				}
				gui::SameLine();
				removePinButton(node.id(), pin);
				gui::PopID();
			}
			gui::BeginDisabled(!editable);
			changed |= renderAddPin(editableGraph, node, "input");
			gui::EndDisabled();
			gui::PopID();
		}

		// Outputs: per GroupOutput node, each pin (editable name, result thumbnail, Save…, ×) + "+".
		gui::Spacing();
		gui::TextUnformatted("Outputs");
		gui::Separator();
		{
			// No null check: the boundary pair is a Graph invariant. The block scopes the ID push.
			const flow::GroupOutputNode& node = graph.boundaryOutputNode();
			flow::GroupOutputNode* const writable = editable ? &editableGraph->boundaryOutputNode() : nullptr;
			gui::PushID(ctx.canvas.node(node.id()));
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& pin = node.input(i);
				gui::PushID(static_cast<int>(pin.id().value()));
				gui::BeginDisabled(!editable);
				renamed |= renderPinName(pin, writable);
				gui::EndDisabled();
				gui::SameLine();
				gui::Text(": %s", std::string(pin.typeName()).c_str());
				if (const char* note = pinNote(pin, carriedOut(pin.id())))
				{
					gui::SameLine();
					gui::TextDisabled("%s", note);
				}

				const PinKey key{ctx.activePath, flow::PortAddress{node.id(), pin.id()}};
				const flow::PortValue& delivered = evaluation.value(key.port);
				if (delivered.empty())
				{
					// The producer was gated off / suppressed (conditional eval) — no value this run.
					// the preview cache already dropped any stale thumbnail; say so rather than show blank.
					gui::TextDisabled("(no output this run)");
				}
				else
				{
					if (const gui::Texture* tex = previews.find(key))
					{
						gui::Image(*tex, previewFit(tex->extent(), thumbnailBox()));
						if (gui::IsItemClicked())
							ctx.previewAsset(key); // click a thumbnail -> full-size in the Preview pane
					}
					if (delivered.holds<image::Image>())
						renderImageSave(ctx, key, delivered.get<image::Image>());
				}
				gui::SameLine();
				removePinButton(node.id(), pin);
				gui::PopID();
			}
			gui::BeginDisabled(!editable);
			changed |= renderAddPin(editableGraph, node, "output");
			gui::EndDisabled();
			gui::PopID();
		}
		// --- Carries (inside a LOOP only) --------------------------------------------------------
		// The pairing is the one fact about a loop's interior that is NOT derivable from it — a
		// carried Image and an invariant Image are the same pin of the same type — so it is the one
		// thing this panel has to STATE rather than let the pins imply, and the one thing it has to
		// author. A document may legitimately pair two DIFFERENTLY named pins, which the per-pin
		// "(carried)" markers alone could not show.
		if (loop != nullptr)
		{
			gui::Spacing();
			gui::TextUnformatted("Carries");
			gui::Separator();

			// `graph` IS this loop's interior: loop is non-null exactly when the active path's last
			// step named one, and that is the graph the panel is drawing.
			const flow::GroupInputNode& into = graph.boundaryInputNode();
			const flow::GroupOutputNode& from = graph.boundaryOutputNode();
			gui::PushID("carries");
			for (const auto& [innerIn, innerOut] : loop->carries())
			{
				const flow::Port* in = into.findOutput(innerIn);
				const flow::Port* out = from.findInput(innerOut);
				if (in == nullptr || out == nullptr)
					continue; // half of it has gone; syncGroupPorts prunes the pairing this frame

				gui::PushID(static_cast<int>(innerIn.value()));
				gui::Text("%s -> %s : %s", in->name().c_str(), out->name().c_str(),
						  std::string(in->typeName()).c_str());
				gui::SameLine();
				gui::BeginDisabled(writableLoop == nullptr);
				if (gui::Button("x"))
				{
					// BOTH pins — the atomic inverse of the paired add. The pairing itself needs no
					// explicit drop: syncGroupPorts calls reconcileInterior() before it touches a
					// port, and the host syncs every group on the path every frame.
					requestRemove("carry '" + in->name() + "'",
								  {flow::PortAddress{into.id(), innerIn}, flow::PortAddress{from.id(), innerOut}});
				}
				gui::EndDisabled();
				gui::PopID();
			}
			if (loop->carries().empty())
				gui::TextDisabled("(none - without one, a loop runs its body N times over the same inputs)");

			gui::BeginDisabled(writableLoop == nullptr);
			changed |= renderAddCarry(writableLoop, into, from);
			gui::EndDisabled();
			gui::PopID();
		}

		gui::End();

		changed |= renderRemoveConfirm(editableGraph);

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
