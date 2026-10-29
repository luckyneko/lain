// rigplan's panes (M15 slice 8e), drawn in a headless ImGui frame: no backend and no device, as
// libs/gui's own frame cases run. Every pane is drawn for every kind of selection, and the frame is held
// to no error ImGui recovers from (an End or a TreePop missing, an id stack left pushed), which ImGui
// counts even where its asserts are compiled out. And the document's guard, which every request that
// would throw away the plan goes through, is driven through the menu bar's own request.

#include "appcontext.h"
#include "editorplan.h"
#include "panes/inspector.h"
#include "panes/issues.h"
#include "panes/menubar.h"
#include "panes/outliner.h"
#include "panes/statusbar.h"
#include "selection.h"

#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/core/uri.h>
#include <lain/editor/guard.h>
#include <lain/gui/gui.h>
#include <lain/io/data/codecs.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h> // GImGui->ErrorCountCurrentFrame: the errors ImGui recovered from this frame

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace rp = lain::rigplan;
using rigplan::Selected;
using rigplan::Selection;

// An ImGui context for a test, with the font atlas built so NewFrame runs, and nothing read or written.
struct HeadlessImGui
{
	ImGuiContext* context = ImGui::CreateContext();

	HeadlessImGui()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1280.0f, 800.0f);
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	}
	~HeadlessImGui() { ImGui::DestroyContext(context); }

	// One frame of `draw`, and how many errors ImGui recovered from in it.
	int frame(const std::function<void()>& draw)
	{
		ImGui::NewFrame();
		draw();
		ImGui::Render();
		return GImGui->ErrorCountCurrentFrame;
	}
};

// An Application for the menu bar's Quit to end, never run.
struct Idle : lain::app::ApplicationDelegate
{
	bool onStart(lain::app::Application&) override { return true; }
};

static std::vector<Selection> everySelection()
{
	return {
		Selection::nothing(),
		Selection::plan(),
		Selection::room(),
		Selection::object(Selected::Mount, editorplan::poleId),
		Selection::object(Selected::Mount, editorplan::bracketId),
		Selection::object(Selected::Camera, editorplan::cam1Id),
		Selection::object(Selected::Light, editorplan::lightId),
		Selection::object(Selected::Plane, editorplan::planeId),
		Selection::object(Selected::Box, editorplan::boxId),
		Selection::object(Selected::Prop, editorplan::propId),
		Selection::object(Selected::Board, editorplan::boardId),
		Selection::keyOf(editorplan::boardId, 1),
		Selection::specificationOf(rigplan::SpecificationKind::Sensor, "hd"),
		Selection::specificationOf(rigplan::SpecificationKind::Lens, "4mm"),
		Selection::specificationOf(rigplan::SpecificationKind::Camera, "hd-4mm"),
		Selection::specificationOf(rigplan::SpecificationKind::Light, "flood"),
	};
}

TEST_CASE("every pane draws every kind of selection, and leaves ImGui nothing to recover", "[rigplan][panes]")
{
	HeadlessImGui imgui;
	Idle idle;
	lain::app::Application app(idle, {"test-rigplan-app", {0, 0, 0}});
	rigplan::AppContext ctx;
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	// A prop that moves, so a prop's key table is drawn too; and a camera on no mount, a problem check()
	// reports with an owner, so the Issues pane draws a row that selects.
	ctx.document.plan.props.front().path = rp::Path{{rp::PathKey{0, {1.0, 1.0, 0.0}, {}}, rp::PathKey{5, {1.5, 1.0, 0.0}, {}}}};
	ctx.document.plan.cameras.back().mount = editorplan::id("a999");

	rigplan::MenuBarPane menuBar;
	rigplan::OutlinerPane outliner;
	rigplan::InspectorPane inspector;
	rigplan::IssuesPane issues;
	rigplan::StatusBarPane statusBar;
	const auto everyPane = [&]
	{
		bool resetLayout = false;
		menuBar.draw(ctx, app, resetLayout);
		outliner.draw(ctx);
		inspector.draw(ctx);
		issues.draw(ctx);
		statusBar.draw(ctx);
		menuBar.drawConfirmModal(ctx, app);
	};
	const lain::data::Value before = rigplan::snapshot(ctx.document.plan);
	for (const Selection& selection : everySelection())
	{
		INFO("selection kind " << int(selection.kind) << " " << selection.name);
		REQUIRE(rigplan::exists(ctx.document.plan, selection));
		ctx.selection = selection;
		CHECK(imgui.frame(everyPane) == 0);
		CHECK(imgui.frame(everyPane) == 0); // a second, with every window and table known
		CHECK(ctx.selection == selection);	// drawing selects nothing
	}
	// Drawing changes nothing either: only an edit does.
	CHECK(rigplan::snapshot(ctx.document.plan) == before);
	CHECK_FALSE(ctx.document.dirty);

	// With every Begin answering false, as ImGui's own check of the Begin/End pairing makes it.
	ImGui::GetIO().ConfigDebugBeginReturnValueOnce = true;
	CHECK(imgui.frame(everyPane) == 0);
}

TEST_CASE("the unsaved-changes question draws, and a held request waits for it", "[rigplan][panes]")
{
	lain::io::data::registerDataCodecs();
	HeadlessImGui imgui;
	Idle idle;
	lain::app::Application app(idle, {"test-rigplan-app", {0, 0, 0}});
	rigplan::AppContext ctx;
	rigplan::MenuBarPane menuBar;

	// With nothing unsaved a New is carried out at once.
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	ctx.selection = Selection::object(Selected::Box, editorplan::boxId);
	CHECK(menuBar.request(ctx, app, {rigplan::DocumentRequest::Kind::New, {}}));
	CHECK(ctx.document.plan.boxes.empty());
	CHECK(ctx.selection == Selection::nothing());

	// With unsaved work it is held, and the question opens on the next frame.
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	ctx.document.dirty = true;
	CHECK_FALSE(menuBar.request(ctx, app, {rigplan::DocumentRequest::Kind::New, {}}));
	CHECK(ctx.document.plan.boxes.size() == 1);
	REQUIRE(ctx.guard.pending());
	bool open = false;
	const auto modal = [&]
	{
		menuBar.drawConfirmModal(ctx, app);
		open = ImGui::IsPopupOpen("Unsaved changes"); // asked inside the frame, where there is a window
	};
	CHECK(imgui.frame(modal) == 0);
	CHECK(open);
	CHECK(imgui.frame(modal) == 0);
	CHECK(open); // it stays open, unanswered

	// Quit is held the same way: the window's close button asks this too (MainWindow::onCloseRequested).
	CHECK_FALSE(menuBar.request(ctx, app, {rigplan::DocumentRequest::Kind::Quit, {}}));
	CHECK(ctx.guard.pending()->kind == rigplan::DocumentRequest::Kind::Quit);
}

TEST_CASE("a plan that will not open keeps the open one, and says why", "[rigplan][panes]")
{
	lain::io::data::registerDataCodecs();
	rigplan::AppContext ctx;
	rigplan::MenuBarPane menuBar;
	const fs::path folder = lain::testing::scratchDir() / "panes-open";
	fs::create_directories(folder);
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	REQUIRE(rigplan::saveDocument(ctx.document, folder / "plan.json").empty());

	// A recent file that has gone leaves the list; the open plan stays, its reasons said.
	ctx.session.plans.recent = {folder / "gone.json", folder / "plan.json"};
	CHECK_FALSE(menuBar.openPath(ctx, folder / "gone.json"));
	CHECK(ctx.document.path == folder / "plan.json");
	CHECK(ctx.session.plans.recent == std::vector<fs::path>{folder / "plan.json"});
	REQUIRE(ctx.openProblems.size() == 1);
	CHECK(ctx.messageIsError);

	// One that opens is the document, at the head of Open Recent.
	rigplan::AppContext other;
	CHECK(menuBar.openPath(other, folder / "plan.json"));
	CHECK(other.document.plan.boxes.size() == 1);
	CHECK(other.session.plans.last == folder / "plan.json");
	CHECK(other.openProblems.empty());
}

TEST_CASE("the session file keeps the plans and the dialog folder, under their keys", "[rigplan][panes]")
{
	lain::io::data::registerDataCodecs();
	const fs::path file = lain::testing::scratchDir() / "rigplan-session.json";
	rigplan::Session written;
	written.plans.last = "/projects/hall/plan.json";
	written.plans.recent = {"/projects/hall/plan.json", "/projects/stage/plan.json"};
	written.lastDialogDir = "/projects";
	rigplan::saveSession(file, written);
	const rigplan::Session read = rigplan::loadSession(file);
	CHECK(read.plans.last == written.plans.last);
	CHECK(read.plans.recent == written.plans.recent);
	CHECK(read.lastDialogDir == written.lastDialogDir);

	// A file of another shape is a fresh session, not an error.
	std::ofstream(file) << "[1, 2, 3]";
	CHECK(rigplan::loadSession(file).plans.recent.empty());
	// And an empty path writes nothing, so a context with none keeps its hands off the home folder.
	rigplan::saveSession({}, written);
}
