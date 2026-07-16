#include "menubar.h"

#include "../appcontext.h"
#include "../flowviewapp.h"
#include "../graphio.h" // loadGraph / saveGraph
#include "../scene.h"	 // nodeCatalog (the Add menu grouping) + buildNewScene

#include <lain/app/application.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/serialize/loadresult.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/gui/nodes.h>
#include <lain/math/types.h>

#include <filesystem>
#include <memory>
#include <string>

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
	}

	void MenuBarPane::requestNew(AppContext& ctx)
	{
		if (ctx.dirty)
			ctx.confirmNew = true; // unsaved changes -> ask first (drawConfirmModal opens the modal)
		else
			newGraph(ctx);
	}

	void MenuBarPane::drawConfirmModal(AppContext& ctx, flow::Graph& graph)
	{
		if (ctx.confirmNew)
		{
			gui::OpenPopup("Unsaved changes");
			ctx.confirmNew = false;
		}
		if (gui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			gui::TextUnformatted("Discard the current graph? Unsaved changes will be lost.");
			if (gui::Button("Save"))
			{
				saveToCurrentPath(ctx, graph); // may prompt for a path if untitled
				newGraph(ctx);
				gui::CloseCurrentPopup();
			}
			gui::SameLine();
			if (gui::Button("Discard"))
			{
				newGraph(ctx);
				gui::CloseCurrentPopup();
			}
			gui::SameLine();
			if (gui::Button("Cancel"))
				gui::CloseCurrentPopup();
			gui::EndPopup();
		}
	}

	void MenuBarPane::openGraphDialog(AppContext& ctx)
	{
		// "All files" fallback: pfd 0.1.0's macOS picker can grey out everything under a lone
		// restrictive filter, so offer an escape hatch alongside the JSON one.
		const auto path = gui::openFile("Open graph", {}, {{"JSON graph", {"*.json"}}, {"All files", {"*"}}});
		if (!path)
			return;
		flow::serialize::LoadResult result = loadGraph(path->string(), ctx.app->nodeFactory());
		// Surface load problems in the Issues panel (not a modal) — they persist until the graph is
		// next edited. Map the serialize severity onto the panel's.
		ctx.loadIssues.clear();
		for (const flow::serialize::LoadIssue& issue : result.issues)
		{
			const Issue::Severity sev = issue.severity == flow::serialize::Severity::Error ? Issue::Severity::Error : Issue::Severity::Warning;
			ctx.loadIssues.push_back({sev, "load: " + issue.message, {}});
		}
		if (result.graph.nodeCount() > 0) // replace the scene — deferred to end of frame
		{
			ctx.loadedGraph = std::make_unique<flow::Graph>(std::move(result.graph));
			ctx.pendingLayout = std::move(result.editor);
			ctx.loadRequested = true;
			ctx.currentPath = *path; // remember for plain Save
			ctx.dirty = false;
		}
	}

	void MenuBarPane::saveToCurrentPath(AppContext& ctx, const flow::Graph& graph)
	{
		if (ctx.currentPath.empty())
		{
			saveAsDialog(ctx, graph); // no file yet -> prompt for one
			return;
		}
		if (saveGraph(ctx.currentPath.string(), graph, ctx.app->nodeFactory(), collectLayout(graph)))
			ctx.dirty = false;
		else
			gui::message("Save failed", "Could not write " + ctx.currentPath.string(), true);
	}

	void MenuBarPane::saveAsDialog(AppContext& ctx, const flow::Graph& graph)
	{
		// Native dialogs block the render thread — the same pattern as the per-output Save… below.
		const auto path = gui::saveFile("Save graph", {}, {{"json", {"*.json"}}});
		if (!path)
			return;
		std::filesystem::path file = *path;
		file.replace_extension("json"); // force .json (the codec is keyed off the extension)
		if (saveGraph(file.string(), graph, ctx.app->nodeFactory(), collectLayout(graph)))
		{
			ctx.currentPath = file; // remember for plain Save
			ctx.dirty = false;
		}
		else
		{
			gui::message("Save failed", "Could not write " + file.string(), true);
		}
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
					openGraphDialog(ctx);
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
			openGraphDialog(ctx);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			saveAsDialog(ctx, graph);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			saveToCurrentPath(ctx, graph);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal))
			app.quit();
	}
} // namespace flowview
