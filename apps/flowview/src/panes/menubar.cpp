#include "menubar.h"

#include "../appcontext.h"
#include "../flowviewapp.h"
#include "../graphio.h" // loadGraph / saveGraph
#include "../scene.h"	 // nodeCatalog (the Add menu grouping) + buildNewScene
#include "../session.h" // noteGraphPath / saveSession (Open Recent + reopen-on-launch)

#include <lain/app/application.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/serialize/loadresult.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/gui/nodes.h>
#include <lain/math/types.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace flowview
{
	using namespace lain;

	// The current canvas positions as an EditorData blob (node id -> {x,y}) for serialization. Reads
	// imnodes grid-space positions, so it must run while the canvas' node ids are live.
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

	void MenuBarPane::newGraph(AppContext& ctx)
	{
		// A blank document — one empty Input + one empty Output node. Deferred to end of frame like
		// every graph swap.
		auto blank = std::make_unique<flow::Graph>();
		buildNewScene(*blank);
		ctx.loadedGraph = std::move(blank);
		ctx.pendingLayout.clear();
		ctx.loadRequested = true;
		ctx.currentPath.clear(); // an untitled document
		ctx.dirty = false;
		ctx.loadIssues.clear();
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
		requestSwap(ctx, {DocumentSwap::New, {}});
	}

	void MenuBarPane::requestOpen(AppContext& ctx, const std::filesystem::path& path)
	{
		requestSwap(ctx, {DocumentSwap::Open, path});
	}

	void MenuBarPane::performSwap(AppContext& ctx, const PendingSwap& swap)
	{
		if (swap.kind == DocumentSwap::New)
			newGraph(ctx);
		else if (swap.path.empty())
			openGraphDialog(ctx); // Open... — pick the file now
		else
			openGraphPath(ctx, swap.path); // Open Recent — the file is already known
	}

	void MenuBarPane::drawConfirmModal(AppContext& ctx, flow::Graph& graph)
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
		flow::serialize::LoadResult result = loadGraph(path.string(), ctx.app->nodeFactory());
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
		ctx.loadRequested = true;
		ctx.currentPath = path; // remember for plain Save
		ctx.dirty = false;
		noteGraphPath(ctx.session, path); // now the document to reopen + the head of Open Recent
		saveSession(ctx.session);
		return true;
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

	bool MenuBarPane::saveToCurrentPath(AppContext& ctx, const flow::Graph& graph)
	{
		if (ctx.currentPath.empty())
			return saveAsDialog(ctx, graph); // no file yet -> prompt for one

		if (saveGraph(ctx.currentPath.string(), graph, ctx.app->nodeFactory(), collectLayout(graph)))
		{
			ctx.dirty = false;
			noteGraphPath(ctx.session, ctx.currentPath); // saving makes it the current document too
			saveSession(ctx.session);
			return true;
		}
		gui::message("Save failed", "Could not write " + ctx.currentPath.string(), true);
		return false;
	}

	bool MenuBarPane::saveAsDialog(AppContext& ctx, const flow::Graph& graph)
	{
		// Native dialogs block the render thread — the same pattern as the per-output Save… below.
		const auto path = gui::saveFile("Save graph", {}, {{"json", {"*.json"}}});
		if (!path)
			return false; // cancelled — the caller must treat this as "not saved"
		std::filesystem::path file = *path;
		file.replace_extension("json"); // force .json (the codec is keyed off the extension)
		if (!saveGraph(file.string(), graph, ctx.app->nodeFactory(), collectLayout(graph)))
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

	void MenuBarPane::draw(AppContext& ctx, flow::Graph& graph, app::Application& app, bool& edited, bool& resetLayout)
	{
		// Cmd on macOS, Ctrl elsewhere — for both the displayed shortcut text and the wired key chord.
		const bool mac = gui::GetIO().ConfigMacOSXBehaviors;
		const std::string m = mac ? "Cmd+" : "Ctrl+";

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
							if (gui::MenuItem(key.c_str()))
							{
								ctx.addCatalogNode(key);
								edited = true;
							}
						}
						gui::EndMenu();
					}
				}
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
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			saveAsDialog(ctx, graph);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			saveToCurrentPath(ctx, graph);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal))
			app.quit();
	}
} // namespace flowview
