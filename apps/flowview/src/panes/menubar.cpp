#include "menubar.h"

#include "../appcontext.h"
#include "../flowviewapp.h"
#include "../graphio.h"	 // loadGraph / saveGraph
#include "../groupnav.h" // enclosingLinkedGroup (Edit Template...)
#include "../scene.h"	 // nodeCatalog (the Add menu grouping) + buildNewScene
#include "../session.h"	 // noteGraphPath / saveSession (Open Recent + reopen-on-launch)
#include "canvasstate.h" // collectLayout (canvas positions for the saved editor section)

#include <lain/app/application.h>
#include <lain/data/value.h> // Value (undo/redo snapshot restored via applyRestore)
#include <lain/flow/edit.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/serialize/loadresult.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/math/types.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace flowview
{
	using namespace lain;

	void MenuBarPane::newGraph(AppContext& ctx)
	{
		// A blank document — a fresh Graph is already one empty Input + one empty Output node.
		// Deferred to end of frame like every graph swap.
		ctx.loadedGraph = std::make_unique<flow::Graph>();
		ctx.pendingLayout = {};
		ctx.pendingPath.reset(); // a New lands at the root, even if an undo was requested first this frame
		ctx.loadRequested = true;
		ctx.pendingBaseline = snapshotGraph(*ctx.loadedGraph, ctx.app->nodeFactory()); // reset undo history to the blank doc
		ctx.currentPath.clear();													   // an untitled document
		ctx.dirty = false;
		ctx.loadIssues.clear();
		ctx.returnStack.clear();	   // a new document is a new lineage — nothing to go back to
		ctx.session.lastGraph.clear(); // nothing to reopen next launch (the recents keep their history)
		saveSession(ctx.session);
	}

	// Ask for a document swap, guarding unsaved work. Both entry points funnel here so New and Open
	// can't diverge: with nothing unsaved the swap happens now, otherwise it waits for the modal.
	void MenuBarPane::requestSwap(AppContext& ctx, PendingSwap swap)
	{
		if (!ctx.dirty)
		{
			performSwap(ctx, swap);
			return;
		}
		ctx.pendingSwap = std::move(swap);
		ctx.confirmSwap = true; // drawConfirmModal opens the modal next time it runs
	}

	void MenuBarPane::requestNew(AppContext& ctx)
	{
		requestSwap(ctx, {DocumentSwap::New, {}, false, std::nullopt});
	}

	void MenuBarPane::requestOpen(AppContext& ctx, const std::filesystem::path& path)
	{
		requestSwap(ctx, {DocumentSwap::Open, path, false, std::nullopt});
	}

	void MenuBarPane::returnToDocument(AppContext& ctx, std::size_t index)
	{
		if (index >= ctx.returnStack.size())
			return;
		// Guarded like any Open — the template may have unsaved edits, which is exactly the work a
		// one-way trip would have thrown away. Everything from `index` on is consumed, so clicking an
		// outer document crumb unwinds several templates at once.
		requestSwap(ctx, {DocumentSwap::Open, ctx.returnStack[index], false, index});
	}

	void MenuBarPane::performSwap(AppContext& ctx, const PendingSwap& swap)
	{
		// Maintain the return stack HERE, where the swap actually happens: by now the guard has been
		// resolved, so a document that was untitled when Edit Template… was clicked has a path if the
		// user chose Save — and a cancelled guard never reaches this point at all.
		if (swap.returnDepth)
		{
			if (*swap.returnDepth < ctx.returnStack.size())
				ctx.returnStack.resize(*swap.returnDepth);
		}
		else if (swap.pushReturn)
		{
			if (ctx.currentPath.empty())
			{
				// An untitled document has no file to come back to, so this really is one-way. Say so
				// rather than opening the template and leaving the user to discover there is no route
				// back (the guard offered a Save a moment ago, which would have given it a path).
				ctx.loadIssues.push_back({Issue::Severity::Warning,
										  "the graph was never saved, so there is no document to return to",
										  {}});
			}
			else
			{
				ctx.returnStack.push_back(ctx.currentPath);
			}
		}
		else if (swap.kind == DocumentSwap::Open)
		{
			ctx.returnStack.clear(); // an unrelated Open leaves the lineage — nothing to go back to
		}

		// A different document: re-read its templates from disk rather than serving whatever the last
		// one resolved. This is also what makes Edit Template... -> Save -> Return show the edit — the
		// return re-opens the parent, and the parent must not be handed the pre-edit definition.
		ctx.templates.clear();

		if (swap.kind == DocumentSwap::New)
			newGraph(ctx);
		else if (swap.path.empty())
			openGraphDialog(ctx); // Open... — pick the file now
		else
			openGraphPath(ctx, swap.path); // Open Recent — the file is already known
	}

	void MenuBarPane::drawConfirmModal(AppContext& ctx, const flow::Graph& graph)
	{
		if (ctx.confirmSwap)
		{
			gui::OpenPopup("Unsaved changes");
			ctx.confirmSwap = false; // the request is consumed; ctx.pendingSwap carries the action
		}
		if (gui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			// Escape closes the modal without resolving; the stale pendingSwap is harmless (nothing
			// reads it outside this block, and the next request overwrites it).
			const PendingSwap swap = ctx.pendingSwap.value_or(PendingSwap{});
			gui::TextUnformatted(swap.kind == DocumentSwap::New
									 ? "Discard the current graph? Unsaved changes will be lost."
									 : "Open another graph? Unsaved changes to the current one will be lost.");
			const auto resolve = [&](bool save)
			{
				// Save may prompt for a path if the document is untitled. Cancelling that panel — or a
				// failed write — cancels the whole swap: never discard work the user just asked to keep.
				const bool proceed = !save || saveToCurrentPath(ctx, graph);
				ctx.pendingSwap.reset();
				gui::CloseCurrentPopup();
				if (proceed)
					performSwap(ctx, swap); // last: an Open... prompts with a native dialog
			};

			if (gui::Button("Save"))
				resolve(true);
			gui::SameLine();
			if (gui::Button("Discard"))
				resolve(false);
			gui::SameLine();
			if (gui::Button("Cancel"))
			{
				ctx.pendingSwap.reset();
				gui::CloseCurrentPopup();
			}
			gui::EndPopup();
		}
	}

	bool MenuBarPane::openGraphPath(AppContext& ctx, const std::filesystem::path& path)
	{
		flow::serialize::LoadResult result = loadGraph(path.string(), ctx.app->nodeFactory(), &ctx.templates);
		// Surface load problems in the Issues panel (not a modal) — they persist until the graph is
		// next edited. Map the serialize severity onto the panel's.
		ctx.loadIssues.clear();
		for (const flow::serialize::LoadIssue& issue : result.issues)
		{
			const Issue::Severity sev = issue.severity == flow::serialize::Severity::Error ? Issue::Severity::Error : Issue::Severity::Warning;
			ctx.loadIssues.push_back({sev, "load: " + issue.message, {}});
		}
		if (result.graph.nodeCount() == 0) // nothing loaded — the issues above say why; keep the current scene
		{
			forgetGraphPath(ctx.session, path); // a moved/deleted file shouldn't linger in Open Recent
			saveSession(ctx.session);
			return false;
		}

		// Replace the scene — deferred to end of frame.
		ctx.loadedGraph = std::make_unique<flow::Graph>(std::move(result.graph));
		ctx.pendingLayout = std::move(result.editor);
		ctx.pendingPath.reset(); // a different document — the root is the honest place to land
		ctx.loadRequested = true;
		// Baseline the undo history to the loaded document (via the same snapshot path push() uses, so
		// the first edit's snapshot compares cleanly). Opening resets history — undo doesn't cross it.
		ctx.pendingBaseline = snapshotGraph(*ctx.loadedGraph, ctx.app->nodeFactory(), ctx.pendingLayout);
		ctx.currentPath = path; // remember for plain Save
		ctx.dirty = false;
		noteGraphPath(ctx.session, path); // now the document to reopen + the head of Open Recent
		saveSession(ctx.session);
		return true;
	}

	bool MenuBarPane::addLinkedGroup(AppContext& ctx)
	{
		const auto picked = gui::openFile("Choose a template graph", gui::lastDirectory(),
										  {{"Graph JSON", {"*.json"}}, {"All files", {"*"}}});
		if (!picked)
			return false;

		const flow::NodeId id = ctx.addCatalogNode("linkedGroup"); // lands in the ACTIVE graph, and is refused inside a link
		if (id == flow::NodeId{})
			return false;
		// addCatalogNode only succeeds where the graph is editable, so this cannot be null here.
		flow::Graph& graph = *resolveEditable(ctx.app->graph(), ctx.activePath);

		auto& linked = static_cast<flow::LinkedGroupNode&>(graph.node(id));

		// Store the path RELATIVE to the current document when we have one, so the pair travels
		// together; an untitled document has no anchor yet, so an absolute path is the honest choice
		// until it is saved.
		const std::filesystem::path chosen = *picked;
		const std::filesystem::path base = ctx.currentPath.parent_path();
		std::error_code ec;
		const std::filesystem::path stored = base.empty() ? chosen : std::filesystem::relative(chosen, base, ec);
		linked.setSource((ec || stored.empty() ? chosen : stored).generic_string());

		// Resolve it now, so the node arrives with its interior and its ports rather than as a
		// placeholder the user has to reload to see — and through the LOADER's own routine, not a
		// second one that happens to do the same thing. This gesture used to load the template as a
		// standalone document, which meant it kept its own copy of a definition every other instance
		// shares, and (before that was noticed) dropped the template's layout that the load path
		// carried. The template's layout lands in this group's subtree of the parent's.
		const flow::serialize::ResolveResult resolved =
			flow::serialize::resolveLinkedGroup(linked, ctx.app->nodeFactory(), sceneCodecs(),
												templateResolver(base), &ctx.templates);
		for (const flow::serialize::LoadIssue& issue : resolved.issues)
		{
			const Issue::Severity sev = issue.severity == flow::serialize::Severity::Error ? Issue::Severity::Error : Issue::Severity::Warning;
			ctx.loadIssues.push_back({sev, "template: " + issue.message, {}});
		}

		GraphPath groupPath = ctx.activePath;
		groupPath.push_back(id);
		layoutAt(ctx.layout, groupPath) = resolved.editor;

		flow::edit::syncGroupPorts(graph, id);
		return true;
	}

	void MenuBarPane::editTemplate(AppContext& ctx, const flow::LinkedGroupNode& linked)
	{
		if (linked.source().empty())
			return;

		// `source` is relative to the document that stores it, and the OUTERMOST link on the path is
		// always stored in the open document — which is why enclosingLinkedGroup returns that one.
		const std::filesystem::path base = ctx.currentPath.parent_path();
		const std::filesystem::path target = base.empty() ? std::filesystem::path(linked.source()) : base / linked.source();

		// The same guarded destructive swap as Open — unsaved work is protected — but flagged to
		// remember where we came from, so this is a round trip and not a one-way door.
		requestSwap(ctx, {DocumentSwap::Open, target, true, std::nullopt});
	}

	void MenuBarPane::undo(AppContext& ctx)
	{
		if (ctx.undo.canUndo())
			applyRestore(ctx, ctx.undo.undo());
	}

	void MenuBarPane::redo(AppContext& ctx)
	{
		if (ctx.undo.canRedo())
			applyRestore(ctx, ctx.undo.redo());
	}

	void MenuBarPane::applyRestore(AppContext& ctx, const data::Value& state)
	{
		// Where the user is, and what they had selected — both carried as plain NodeIds. A restore
		// rebuilds the graph from a document, and a document RESTORES identity (ADR-0011), so the ids
		// mean the same thing on the far side. That keeps a param-drag undo from dropping the canvas
		// selection (and with it the selection-driven Inspector), and keeps an undo made INSIDE a
		// group from ejecting the user to the root, away from the edit they just undid.
		ctx.pendingPath = ctx.activePath;
		ctx.pendingReselect = selectedNodes(ctx.canvas);

		// Rebuild from the snapshot and route it through the same deferred-swap path as a load — but
		// with NO pendingBaseline, so the swap handler keeps the history (the cursor already moved).
		// The document's own folder, so a linked group's relative `source` resolves as it did on load.
		// The cache is NOT cleared here: an undo is not a document change, and keeping it is what
		// holds a template's inner ids — and the preview keys and canvas ints built on them — steady.
		flow::serialize::LoadResult result = restoreGraph(state, ctx.app->nodeFactory(), ctx.currentPath.parent_path(),
														  &ctx.templates);
		ctx.loadedGraph = std::make_unique<flow::Graph>(std::move(result.graph));
		ctx.pendingLayout = std::move(result.editor); // the WHOLE tree: inner levels keep their layout too
		// NOTE: pendingPath was set at the top of this function and must survive — it is what keeps an
		// undo from ejecting the user to the root. (New/Open clear it; a restore deliberately does not.)
		ctx.loadRequested = true;
		ctx.dirty = true; // a restored state differs from what's on disk (in general)
		ctx.loadIssues.clear();
	}

	void MenuBarPane::openGraphDialog(AppContext& ctx)
	{
		// "All files" fallback: pfd 0.1.0's macOS picker can grey out everything under a lone
		// restrictive filter, so offer an escape hatch alongside the JSON one.
		const auto path = gui::openFile("Open graph", {}, {{"JSON graph", {"*.json"}}, {"All files", {"*"}}});
		if (!path)
			return;
		openGraphPath(ctx, *path);
	}

	void MenuBarPane::drawOpenRecent(AppContext& ctx)
	{
		const std::vector<std::filesystem::path>& recent = ctx.session.recentGraphs;
		if (!gui::BeginMenu("Open Recent", !recent.empty())) // greyed out with no history
			return;

		// Act after the menu closes: opening prunes the list on failure, so the loop must not be
		// iterating it. The label is the filename (paths are too wide for a menu) with the full path
		// as a tooltip; the index PushID keeps two same-named files in different folders distinct.
		std::filesystem::path chosen;
		for (std::size_t i = 0; i < recent.size(); ++i)
		{
			gui::PushID(static_cast<int>(i));
			if (gui::MenuItem(recent[i].filename().string().c_str()))
				chosen = recent[i];
			if (gui::IsItemHovered())
				gui::SetTooltip("%s", recent[i].string().c_str());
			gui::PopID();
		}
		gui::Separator();
		const bool clear = gui::MenuItem("Clear Menu");
		gui::EndMenu();

		if (clear)
		{
			ctx.session.recentGraphs.clear();
			saveSession(ctx.session);
		}
		else if (!chosen.empty())
		{
			requestOpen(ctx, chosen); // guarded like Open... — replacing the graph discards unsaved work
		}
	}

	// Bring the layout tree up to date for the level currently on screen, then hand back the DOCUMENT
	// to write. Saving is a document operation, never a view one: the panes are pointed at the active
	// graph, but the file is always the whole root graph plus every level's positions. (Writing the
	// active graph would overwrite the document with just the group you happened to be inside.)
	const flow::Graph& MenuBarPane::documentToSave(AppContext& ctx, const flow::Graph& activeGraph)
	{
		layoutAt(ctx.layout, ctx.activePath).nodes = collectLayout(ctx.canvas, activeGraph);
		return ctx.app->graph();
	}

	bool MenuBarPane::saveToCurrentPath(AppContext& ctx, const flow::Graph& activeGraph)
	{
		if (ctx.currentPath.empty())
			return saveAsDialog(ctx, activeGraph); // no file yet -> prompt for one

		const flow::Graph& document = documentToSave(ctx, activeGraph);
		if (saveGraph(ctx.currentPath.string(), document, ctx.app->nodeFactory(), ctx.layout))
		{
			ctx.dirty = false;
			noteGraphPath(ctx.session, ctx.currentPath); // saving makes it the current document too
			saveSession(ctx.session);
			return true;
		}
		gui::message("Save failed", "Could not write " + ctx.currentPath.string(), true);
		return false;
	}

	bool MenuBarPane::saveAsDialog(AppContext& ctx, const flow::Graph& activeGraph)
	{
		// Native dialogs block the render thread — the same pattern as the per-output Save… below.
		const auto path = gui::saveFile("Save graph", {}, {{"json", {"*.json"}}});
		if (!path)
			return false; // cancelled — the caller must treat this as "not saved"
		std::filesystem::path file = *path;
		file.replace_extension("json"); // force .json (the codec is keyed off the extension)
		const flow::Graph& document = documentToSave(ctx, activeGraph);
		if (!saveGraph(file.string(), document, ctx.app->nodeFactory(), ctx.layout))
		{
			gui::message("Save failed", "Could not write " + file.string(), true);
			return false;
		}
		ctx.currentPath = file; // remember for plain Save
		ctx.dirty = false;
		noteGraphPath(ctx.session, file);
		saveSession(ctx.session);
		return true;
	}

	void MenuBarPane::draw(AppContext& ctx, const flow::Graph& graph, app::Application& app, bool& edited,
						   bool& resetLayout)
	{
		// Cmd on macOS, Ctrl elsewhere — for both the displayed shortcut text and the wired key chord.
		const bool mac = gui::GetIO().ConfigMacOSXBehaviors;
		const std::string m = mac ? "Cmd+" : "Ctrl+";

		// The breadcrumb's document crumbs and its Edit button route here: document swaps belong to the
		// menu bar, and the canvas draws earlier in the frame.
		if (ctx.returnRequest)
		{
			const std::size_t index = *ctx.returnRequest;
			ctx.returnRequest.reset();
			returnToDocument(ctx, index);
		}
		if (ctx.editTemplateRequested)
		{
			ctx.editTemplateRequested = false;
			if (const flow::LinkedGroupNode* linked = enclosingLinkedGroup(ctx.app->graph(), ctx.activePath))
				editTemplate(ctx, *linked);
		}

		if (gui::BeginMainMenuBar())
		{
			if (gui::BeginMenu("File"))
			{
				if (gui::MenuItem("New", (m + "N").c_str()))
					requestNew(ctx);
				if (gui::MenuItem("Open...", (m + "O").c_str()))
					requestOpen(ctx);
				drawOpenRecent(ctx);
				if (gui::MenuItem("Save", (m + "S").c_str()))
					saveToCurrentPath(ctx, graph);
				if (gui::MenuItem("Save As...", (m + "Shift+S").c_str()))
					saveAsDialog(ctx, graph);
				gui::Separator();
				if (gui::MenuItem("Quit", (m + "Q").c_str()))
					app.quit();
				gui::EndMenu();
			}
			if (gui::BeginMenu("Edit"))
			{
				if (gui::MenuItem("Undo", (m + "Z").c_str(), false, ctx.undo.canUndo()))
					undo(ctx);
				if (gui::MenuItem("Redo", (m + "Shift+Z").c_str(), false, ctx.undo.canRedo()))
					redo(ctx);
				gui::EndMenu();
			}
			if (gui::BeginMenu("Add"))
			{
				// Grouped by category (nodeCatalog); each item cascades its grid position so successive
				// adds don't stack. A menu route to the palette, alongside the canvas right-click + panel.
				for (const NodeCategory& category : nodeCatalog())
				{
					if (gui::BeginMenu(category.name.c_str()))
					{
						for (const std::string& key : category.keys)
						{
							// addCatalogNode refuses inside a linked group (and lands in the ACTIVE graph),
							// so respect its answer rather than assuming an edit happened.
							if (gui::MenuItem(key.c_str()))
								edited |= ctx.addCatalogNode(key) != flow::NodeId{};
						}
						gui::EndMenu();
					}
				}

				// A LINKED group is not in the catalog: it needs its template picked first, so it
				// arrives through a file dialog rather than off a list.
				gui::Separator();
				if (gui::MenuItem("Linked Group..."))
					edited |= addLinkedGroup(ctx);
				gui::EndMenu();
			}
			if (gui::BeginMenu("View"))
			{
				if (gui::MenuItem("Reset Layout"))
					resetLayout = true; // re-stamps the default dock layout next frame
				gui::EndMenu();
			}
			gui::EndMainMenuBar();
		}

		// Global shortcuts via Shortcut()+RouteGlobal (fires regardless of focus). Always use
		// ImGuiMod_Ctrl: ImGui remaps it to Cmd on macOS (ConfigMacOSXBehaviors), so an explicit
		// ImGuiMod_Super would NOT match. Mods match exactly, so Ctrl+Shift+S and Ctrl+S don't collide.
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal))
			requestNew(ctx);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal))
			requestOpen(ctx);
		// Undo / redo: Ctrl+Z and Ctrl+Shift+Z (mods match exactly, so the two don't collide).
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
			undo(ctx);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
			redo(ctx);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			saveAsDialog(ctx, graph);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			saveToCurrentPath(ctx, graph);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal))
			app.quit();
	}
} // namespace flowview
