#include "menubar.h"

#include "fields.h" // meshFilters, for Add > Prop...

#include <lain/app/application.h>
#include <lain/core/uri.h>
#include <lain/editor/recents.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/string/format.h>

#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace rigplan
{
	using namespace lain;
	namespace fs = std::filesystem;

	void MenuBarPane::apply(AppContext& ctx, const EditResult& result)
	{
		if (result.changed())
		{
			ctx.selection = *result.selection;
			ctx.changed();
		}
		else if (!result.refusal.empty())
			ctx.note(result.refusal, true);
	}

	bool MenuBarPane::request(AppContext& ctx, app::Application& app, DocumentRequest request)
	{
		std::optional<DocumentRequest> now = ctx.guard.request(std::move(request), ctx.document.dirty);
		if (!now)
			return false;
		perform(ctx, app, *now);
		return true;
	}

	void MenuBarPane::perform(AppContext& ctx, app::Application& app, const DocumentRequest& request)
	{
		switch (request.kind)
		{
			case DocumentRequest::Kind::New:
				ctx.document = newDocument();
				ctx.selection = Selection::nothing();
				ctx.openProblems.clear();
				ctx.session.plans.last.clear(); // nothing to reopen at the next launch; the recents stay
				ctx.rememberSession();
				return;
			case DocumentRequest::Kind::Open:
				if (request.path.empty())
				{
					// "All files" beside the filter: pfd's macOS picker can grey out everything under a
					// lone restrictive one.
					if (const auto picked = gui::openFile("Open rig plan", {}, {{"Rig plan", {"*.json"}}, {"All files", {"*"}}}))
						openPath(ctx, *picked);
				}
				else
					openPath(ctx, request.path);
				return;
			case DocumentRequest::Kind::Quit:
				app.exit();
				return;
		}
	}

	bool MenuBarPane::openPath(AppContext& ctx, const fs::path& path)
	{
		DocumentRead opened = openDocument(path);
		if (!opened.document)
		{
			ctx.openProblems = std::move(opened.problems);
			ctx.note(string::format("{} did not open, so the current plan is kept", path.filename().string()), true);
			// A moved or deleted file leaves Open Recent, and is not retried at every launch. One that is
			// there stays: it may want a newer build, not another path.
			std::error_code ec;
			if (!fs::exists(path, ec))
			{
				editor::forgetDocument(ctx.session.plans, path);
				ctx.rememberSession();
			}
			return false;
		}
		ctx.document = std::move(*opened.document);
		ctx.selection = Selection::nothing();
		ctx.openProblems.clear();
		editor::noteDocument(ctx.session.plans, ctx.document.path);
		ctx.rememberSession();
		return true;
	}

	bool MenuBarPane::save(AppContext& ctx)
	{
		if (ctx.document.path.empty())
			return saveAs(ctx);
		const std::vector<std::string> problems = saveDocument(ctx.document, ctx.document.path);
		if (!problems.empty())
		{
			gui::message("Save failed", problems.front(), true);
			return false;
		}
		editor::noteDocument(ctx.session.plans, ctx.document.path);
		ctx.rememberSession();
		return true;
	}

	bool MenuBarPane::saveAs(AppContext& ctx)
	{
		const auto picked = gui::saveFile("Save rig plan", {}, {{"json", {"*.json"}}});
		if (!picked)
			return false; // cancelled: not saved, so a request waiting on it is cancelled too
		fs::path file = *picked;
		file.replace_extension("json"); // the codec is chosen by the extension
		const std::vector<std::string> problems = saveDocument(ctx.document, file);
		if (!problems.empty())
		{
			gui::message("Save failed", problems.front(), true);
			return false;
		}
		editor::noteDocument(ctx.session.plans, ctx.document.path);
		ctx.rememberSession();
		return true;
	}

	void MenuBarPane::drawOpenRecent(AppContext& ctx, app::Application& app)
	{
		const std::vector<fs::path>& recent = ctx.session.plans.recent;
		if (!gui::BeginMenu("Open Recent", !recent.empty()))
			return;
		// Acted on after the menu closes: opening prunes the list on failure, so the loop must not be
		// walking it.
		fs::path chosen;
		for (std::size_t i = 0; i < recent.size(); ++i)
		{
			gui::PushID(int(i));
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
			ctx.session.plans.recent.clear();
			ctx.rememberSession();
		}
		else if (!chosen.empty())
			request(ctx, app, {DocumentRequest::Kind::Open, chosen});
	}

	// Whether a camera or light can be added for this selection: it names a mount, or a camera or light
	// on one.
	static bool onAMount(const Selection& selection)
	{
		return selection.kind == Selected::Mount || selection.kind == Selected::Camera || selection.kind == Selected::Light;
	}

	// Add > Prop...: a mesh picked, then a prop showing it.
	static void pickProp(AppContext& ctx)
	{
		if (const auto picked = gui::openFile("Choose a prop's mesh", gui::lastDirectory(), fields::meshFilters()))
		{
			const EditResult result = addProp(ctx.document.plan, core::Uri::fromPath(*picked));
			if (result.changed())
			{
				ctx.selection = *result.selection;
				ctx.changed();
			}
		}
	}

	void MenuBarPane::draw(AppContext& ctx, app::Application& app, bool& resetLayout)
	{
		const bool mac = gui::GetIO().ConfigMacOSXBehaviors;
		const std::string m = mac ? "Cmd+" : "Ctrl+";
		lain::rigplan::RigPlan& plan = ctx.document.plan;

		if (gui::BeginMainMenuBar())
		{
			if (gui::BeginMenu("File"))
			{
				if (gui::MenuItem("New", (m + "N").c_str()))
					request(ctx, app, {DocumentRequest::Kind::New, {}});
				if (gui::MenuItem("Open...", (m + "O").c_str()))
					request(ctx, app, {DocumentRequest::Kind::Open, {}});
				drawOpenRecent(ctx, app);
				if (gui::MenuItem("Save", (m + "S").c_str()))
					save(ctx);
				if (gui::MenuItem("Save As...", (m + "Shift+S").c_str()))
					saveAs(ctx);
				gui::Separator();
				if (gui::MenuItem("Quit", (m + "Q").c_str()))
					request(ctx, app, {DocumentRequest::Kind::Quit, {}});
				gui::EndMenu();
			}
			if (gui::BeginMenu("Edit"))
			{
				if (gui::MenuItem("Undo", (m + "Z").c_str(), false, ctx.document.history.canUndo() || ctx.document.pendingSnapshot))
					undo(ctx.document);
				if (gui::MenuItem("Redo", (m + "Shift+Z").c_str(), false, ctx.document.history.canRedo()))
					redo(ctx.document);
				gui::Separator();
				const bool something = ctx.selection.kind != Selected::Nothing && ctx.selection.kind != Selected::Plan;
				if (gui::MenuItem("Duplicate", (m + "D").c_str(), false, something))
					apply(ctx, duplicate(plan, ctx.selection));
				if (gui::MenuItem("Delete", "Delete", false, something))
					apply(ctx, remove(plan, ctx.selection));
				gui::EndMenu();
			}
			if (gui::BeginMenu("Add"))
			{
				if (gui::MenuItem("Pole"))
					apply(ctx, addPole(plan));
				if (gui::MenuItem("Fixed Point"))
					apply(ctx, addFixedPoint(plan));
				gui::Separator();
				if (gui::MenuItem("Camera", nullptr, false, onAMount(ctx.selection)))
					apply(ctx, addCamera(plan, ctx.selection));
				if (gui::MenuItem("Light", nullptr, false, onAMount(ctx.selection)))
					apply(ctx, addLight(plan, ctx.selection));
				if (!onAMount(ctx.selection) && gui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					gui::SetTooltip("Select a mount to put it on");
				gui::Separator();
				if (gui::MenuItem("Plane"))
					apply(ctx, addPlane(plan));
				if (gui::MenuItem("Box"))
					apply(ctx, addBox(plan));
				if (gui::MenuItem("Prop..."))
					pickProp(ctx);
				if (gui::MenuItem("Board"))
					apply(ctx, addBoard(plan));
				if (gui::MenuItem("Room", nullptr, false, !plan.room))
					apply(ctx, addRoom(plan));
				gui::EndMenu();
			}
			if (gui::BeginMenu("View"))
			{
				if (gui::MenuItem("Reset Layout"))
					resetLayout = true;
				gui::EndMenu();
			}
			gui::EndMainMenuBar();
		}

		// Global shortcuts. ImGuiMod_Ctrl is Cmd on macOS (ConfigMacOSXBehaviors), and mods match exactly,
		// so Ctrl+S and Ctrl+Shift+S do not collide.
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal))
			request(ctx, app, {DocumentRequest::Kind::New, {}});
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal))
			request(ctx, app, {DocumentRequest::Kind::Open, {}});
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			saveAs(ctx);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
			save(ctx);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal))
			request(ctx, app, {DocumentRequest::Kind::Quit, {}});
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
			undo(ctx.document);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
			redo(ctx.document);
		if (gui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, ImGuiInputFlags_RouteGlobal) && ctx.selection.kind != Selected::Plan)
			apply(ctx, duplicate(plan, ctx.selection));
		// Delete and Backspace delete what is selected, but never while a text field has them.
		if (!gui::GetIO().WantTextInput && (gui::IsKeyPressed(ImGuiKey_Delete, false) || gui::IsKeyPressed(ImGuiKey_Backspace, false)) &&
			ctx.selection.kind != Selected::Plan)
			apply(ctx, remove(plan, ctx.selection));
	}

	void MenuBarPane::drawConfirmModal(AppContext& ctx, app::Application& app)
	{
		if (ctx.guard.takePrompt())
			gui::OpenPopup("Unsaved changes");
		if (gui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			// Escape closes it without an answer; the request left in the guard is harmless, and the next
			// request replaces it.
			const DocumentRequest* pending = ctx.guard.pending();
			const DocumentRequest::Kind kind = pending ? pending->kind : DocumentRequest::Kind::New;
			gui::TextUnformatted(kind == DocumentRequest::Kind::New	   ? "Start a new plan? Unsaved changes to this one will be lost."
								 : kind == DocumentRequest::Kind::Open ? "Open another plan? Unsaved changes to this one will be lost."
																	   : "Quit? Unsaved changes to this plan will be lost.");
			const auto resolve = [&](editor::UnsavedAnswer answer)
			{
				// A Save that is cancelled (an untitled plan's panel closed) or fails cancels the request:
				// work the user just asked to keep is never thrown away.
				const std::optional<DocumentRequest> now = ctx.guard.answer(answer, [&]
																			{ return save(ctx); });
				gui::CloseCurrentPopup();
				if (now)
					perform(ctx, app, *now); // last: an Open... asks with a native dialog
			};
			if (gui::Button("Save"))
				resolve(editor::UnsavedAnswer::Save);
			gui::SameLine();
			if (gui::Button("Discard"))
				resolve(editor::UnsavedAnswer::Discard);
			gui::SameLine();
			if (gui::Button("Cancel"))
				resolve(editor::UnsavedAnswer::Cancel);
			gui::EndPopup();
		}
	}
} // namespace rigplan
