// rigplan's panes (M15 slice 8e), drawn in a headless ImGui frame: no backend and no device, as
// libs/gui's own frame cases run. Every pane is drawn for every kind of selection, and the frame is held
// to no error ImGui recovers from (an End or a TreePop missing, an id stack left pushed), which ImGui
// counts even where its asserts are compiled out. And the document's guard, which every request that
// would throw away the plan goes through, is driven through the menu bar's own request, as K and Space are
// through the keys a frame is handed (9b). And the Specifications menu's picker and clash question (9j),
// driven past their native panels, as the menu's items are once a file is picked.

#include "appcontext.h"
#include "editorplan.h"
#include "panes/inspector.h"
#include "panes/issues.h"
#include "panes/keys.h"
#include "panes/menubar.h"
#include "panes/outliner.h"
#include "panes/specifications.h"
#include "panes/statusbar.h"
#include "panes/timeline.h"
#include "selection.h"

#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/editor/guard.h>
#include <lain/gui/gui.h>
#include <lain/io/data/codecs.h>
#include <lain/io/data/save.h>
#include <lain/rigplan/serialize/rigplan.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h> // GImGui->ErrorCountCurrentFrame: the errors ImGui recovered from this frame

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
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
	rigplan::TimelinePane timeline;
	rigplan::IssuesPane issues;
	rigplan::StatusBarPane statusBar;
	const auto everyPane = [&]
	{
		bool resetLayout = false;
		menuBar.draw(ctx, app, resetLayout);
		outliner.draw(ctx);
		inspector.draw(ctx);
		timeline.draw(ctx);
		issues.draw(ctx);
		statusBar.draw(ctx);
		menuBar.drawModals(ctx, app);
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

TEST_CASE("the Timeline draws a timeline of one frame, keys past it and a plan with nothing moving", "[rigplan][panes]")
{
	HeadlessImGui imgui;
	rigplan::AppContext ctx;
	rigplan::TimelinePane timeline;
	const auto draw = [&]
	{ timeline.draw(ctx); };

	// One frame, so the board's key at 10 and the prop's at 5 are both past it, in the strip.
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	ctx.document.plan.props.front().path = rp::Path{{rp::PathKey{0, {1.0, 1.0, 0.0}, {}}, rp::PathKey{5, {1.5, 1.0, 0.0}, {}}}};
	ctx.document.plan.timeline.frames = 1;
	for (const Selection& selection : everySelection())
	{
		INFO("selection kind " << int(selection.kind) << " " << selection.name);
		ctx.selection = selection;
		ctx.followFrame(); // a key past the timeline is the frame shown while it is selected
		CHECK(imgui.frame(draw) == 0);
		CHECK(imgui.frame(draw) == 0);
		CHECK(ctx.selection == selection);
	}

	// Nothing moves in a New plan: the pane says so.
	ctx.document = rigplan::newDocument();
	ctx.selection = Selection::nothing();
	CHECK(imgui.frame(draw) == 0);
	ImGui::GetIO().ConfigDebugBeginReturnValueOnce = true;
	CHECK(imgui.frame(draw) == 0);
}

TEST_CASE("K keys the selected mover at the frame shown, and Space plays the timeline at its rate", "[rigplan][panes]")
{
	HeadlessImGui imgui;
	Idle idle;
	lain::app::Application app(idle, {"test-rigplan-app", {0, 0, 0}});
	rigplan::AppContext ctx;
	rigplan::MenuBarPane menuBar;
	rigplan::TimelinePane timeline;
	const auto draw = [&]
	{
		bool resetLayout = false;
		menuBar.draw(ctx, app, resetLayout);
		timeline.draw(ctx);
	};
	// One frame with `key` down, and one with it let go.
	const auto press = [&](ImGuiKey key)
	{
		ImGui::GetIO().AddKeyEvent(key, true);
		CHECK(imgui.frame(draw) == 0);
		ImGui::GetIO().AddKeyEvent(key, false);
		CHECK(imgui.frame(draw) == 0);
	};

	// The board, at frame 3 of 12: K makes its key there, selected, and the frame stays.
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	ctx.selection = Selection::object(Selected::Board, editorplan::boardId);
	ctx.frame = 3;
	press(ImGuiKey_K);
	const rp::Path& path = ctx.document.plan.boards.front().path;
	REQUIRE(path.keys.size() == 3);
	CHECK(path.keys[1].frame == 3);
	CHECK(ctx.selection == Selection::keyOf(editorplan::boardId, 1));
	CHECK(ctx.document.dirty);
	ctx.followFrame();
	CHECK(ctx.frame == 3);
	// Again, on the key it made: that frame is keyed already, so nothing changes.
	press(ImGuiKey_K);
	CHECK(path.keys.size() == 3);
	// With a static prop selected, K says why it cannot, and keys nothing.
	ctx.selection = Selection::object(Selected::Prop, editorplan::propId);
	press(ImGuiKey_K);
	CHECK_FALSE(ctx.document.plan.props.front().path);
	CHECK(ctx.messageIsError);

	// Space plays: at no stated rate the timeline plays at 24 fps, so 0.26 s is six frames and a little,
	// and the twelve frames go round. The frame pressed in starts the clock, and moves nothing.
	ctx.frame = 0;
	ImGui::GetIO().DeltaTime = 0.26f; // six frames and a little over, each time
	REQUIRE_FALSE(ctx.document.plan.timeline.rate.specified());
	press(ImGuiKey_Space);
	CHECK(ctx.playback.playing());
	CHECK(ctx.frame == 6); // the frame Space was let go in
	CHECK(imgui.frame(draw) == 0);
	CHECK(ctx.frame == 0);
	CHECK(imgui.frame(draw) == 0);
	CHECK(ctx.frame == 6);
	press(ImGuiKey_Space);
	CHECK_FALSE(ctx.playback.playing());
	const std::size_t paused = ctx.frame;
	CHECK(imgui.frame(draw) == 0);
	CHECK(ctx.frame == paused);
}

TEST_CASE("the Timeline's clock runs while its pane is not drawn", "[rigplan][panes]")
{
	// ImGui's Begin answers false the first time a window is made, under this flag, as it does for a tab
	// behind another: the frame still moves on.
	HeadlessImGui imgui;
	ImGui::GetIO().ConfigDebugBeginReturnValueOnce = true;
	ImGui::GetIO().DeltaTime = 0.26f;
	rigplan::AppContext ctx;
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	ctx.playback.play();
	rigplan::TimelinePane timeline;
	CHECK(imgui.frame([&]
					  { timeline.draw(ctx); }) == 0);
	CHECK(ctx.frame == 6);
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
		menuBar.drawModals(ctx, app);
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

static std::string bytesOf(const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A catalogue whose "hd" is a 1080p sensor where the New plan's is 720p, whose "4mm" is the plan's
// exactly, and whose "hd-4mm" is made of the two: written as a catalogue document at `file`.
static void writeCatalogue(const fs::path& file)
{
	const rp::RigPlan fresh = rigplan::newPlan();
	rp::SensorSpecification hd = *rp::findSensor(fresh.specifications, "hd");
	hd.image = {1920, 1080};
	hd.pixelPitch = lain::core::Length::from<lain::core::Length::Micrometres>(3.0);
	rp::Specifications c;
	c.sensors = {hd};
	c.lenses = {*rp::findLens(fresh.specifications, "4mm")};
	c.cameras = {{"hd-4mm", "hd", "4mm"}};
	REQUIRE(lain::io::data::save(lain::core::Uri::fromPath(file), rp::catalogueToValue(c)));
}

TEST_CASE("Copy In from Catalogue opens a picker on a catalogue, and copies what it picks as one step", "[rigplan][panes][catalogue]")
{
	lain::io::data::registerDataCodecs();
	HeadlessImGui imgui;
	Idle idle;
	lain::app::Application app(idle, {"test-rigplan-app", {0, 0, 0}});
	rigplan::AppContext ctx;
	rigplan::MenuBarPane menuBar;
	rigplan::SpecificationsMenu specifications;
	const fs::path folder = lain::testing::scratchDir() / "panes-copy-in";
	fs::create_directories(folder);
	const fs::path file = folder / "catalogue.json";
	writeCatalogue(file);

	bool open = false;
	const auto modals = [&]
	{
		menuBar.drawModals(ctx, app);
		open = ImGui::IsPopupOpen("Copy In from Catalogue");
	};

	// It opens on the first specification of the first kind the catalogue has: a camera specification.
	const lain::data::Value before = rigplan::snapshot(ctx.document.plan);
	REQUIRE(specifications.beginCopyIn(ctx, file));
	REQUIRE(ctx.copyingIn);
	CHECK(ctx.copyingIn->kind == rigplan::SpecificationKind::Camera);
	CHECK(ctx.copyingIn->name == "hd-4mm");
	CHECK(imgui.frame(modals) == 0);
	CHECK(open);

	// Every kind draws, one the catalogue has none of and one whose pick the plan has already included.
	for (const auto& [kind, name] : std::vector<std::pair<rigplan::SpecificationKind, std::string>>{
			 {rigplan::SpecificationKind::Sensor, "hd"},
			 {rigplan::SpecificationKind::Lens, "4mm"},
			 {rigplan::SpecificationKind::Light, ""},
			 {rigplan::SpecificationKind::Camera, "hd-4mm"}})
	{
		INFO("kind " << int(kind));
		ctx.copyingIn->kind = kind;
		ctx.copyingIn->name = name;
		CHECK(imgui.frame(modals) == 0);
		CHECK(open);
	}
	CHECK(rigplan::snapshot(ctx.document.plan) == before); // drawing it copies nothing

	// Copied: the camera specification arrives numbered, its 1080p sensor with it and numbered too, its lens
	// the plan's already. Selected, said, and the picker closed.
	specifications.copyPicked(ctx);
	CHECK_FALSE(ctx.copyingIn);
	const rp::CameraSpecification* arrived = rp::findCamera(ctx.document.plan.specifications, "hd-4mm 2");
	REQUIRE(arrived);
	CHECK(arrived->sensor == "hd 2");
	CHECK(arrived->lens == "4mm");
	CHECK(ctx.selection == Selection::specificationOf(rigplan::SpecificationKind::Camera, "hd-4mm 2"));
	CHECK(ctx.message == "Copied in camera specification 'hd-4mm 2' and sensor 'hd 2' from catalogue.json");
	CHECK_FALSE(ctx.messageIsError);
	CHECK(ctx.document.dirty);
	CHECK(imgui.frame(modals) == 0);
	CHECK_FALSE(open);

	// One step of history.
	rigplan::commit(ctx.document);
	REQUIRE(rigplan::undo(ctx.document));
	CHECK(rigplan::snapshot(ctx.document.plan) == before);
	CHECK_FALSE(rigplan::undo(ctx.document));

	// A question opened over the picker (the unsaved-changes one, asked by a shortcut) closes it, and it is
	// let go of rather than left waiting.
	REQUIRE(specifications.beginCopyIn(ctx, file));
	CHECK(imgui.frame(modals) == 0);
	REQUIRE(open);
	ctx.document.dirty = true;
	REQUIRE_FALSE(menuBar.request(ctx, app, {rigplan::DocumentRequest::Kind::New, {}}));
	CHECK(imgui.frame(modals) == 0);
	CHECK_FALSE(open);
	CHECK_FALSE(ctx.copyingIn);
	ctx.guard.answer(lain::editor::UnsavedAnswer::Cancel, []
					 { return false; });
	CHECK(imgui.frame(modals) == 0);

	// What the plan has already changes nothing, and is selected.
	REQUIRE(specifications.beginCopyIn(ctx, file));
	ctx.copyingIn->kind = rigplan::SpecificationKind::Lens;
	ctx.copyingIn->name = "4mm";
	specifications.copyPicked(ctx);
	CHECK(rigplan::snapshot(ctx.document.plan) == before);
	CHECK(ctx.selection == Selection::specificationOf(rigplan::SpecificationKind::Lens, "4mm"));
	CHECK_FALSE(ctx.messageIsError);
}

TEST_CASE("Copy In from a file that is not a catalogue opens no picker, and says why", "[rigplan][panes][catalogue]")
{
	lain::io::data::registerDataCodecs();
	rigplan::AppContext ctx;
	rigplan::SpecificationsMenu specifications;
	const fs::path folder = lain::testing::scratchDir() / "panes-copy-in-refused";
	fs::create_directories(folder);

	// A rig plan is a document, of another kind.
	const fs::path plan = folder / "plan.json";
	REQUIRE(rigplan::saveDocument(ctx.document, plan).empty());
	CHECK_FALSE(specifications.beginCopyIn(ctx, plan));
	CHECK_FALSE(ctx.copyingIn);
	CHECK(ctx.messageIsError);

	ctx.message.clear();
	ctx.messageIsError = false;
	CHECK_FALSE(specifications.beginCopyIn(ctx, folder / "gone.json"));
	CHECK_FALSE(ctx.copyingIn);
	CHECK(ctx.messageIsError);
}

TEST_CASE("Save to Catalogue saves the selected specification, or asks about a clash and does what the answer says",
		  "[rigplan][panes][catalogue]")
{
	lain::io::data::registerDataCodecs();
	HeadlessImGui imgui;
	Idle idle;
	lain::app::Application app(idle, {"test-rigplan-app", {0, 0, 0}});
	rigplan::AppContext ctx;
	rigplan::MenuBarPane menuBar;
	rigplan::SpecificationsMenu specifications;
	const fs::path folder = lain::testing::scratchDir() / "panes-save-out";
	fs::create_directories(folder);
	bool open = false;
	const auto modals = [&]
	{
		menuBar.drawModals(ctx, app);
		open = ImGui::IsPopupOpen("Already in the Catalogue");
	};
	const auto width = [](const fs::path& file, const std::string& name) -> std::uint32_t
	{
		const rigplan::CatalogueFileRead read = rigplan::readCatalogue(file);
		REQUIRE(read.specifications);
		const rp::SensorSpecification* s = rp::findSensor(*read.specifications, name);
		return s ? s->image.width : 0;
	};

	// With no specification selected there is nothing to save.
	specifications.saveSelected(ctx, folder / "nothing.json", rigplan::CatalogueTarget::New);
	CHECK(ctx.messageIsError);
	CHECK_FALSE(fs::exists(folder / "nothing.json"));

	// To a new catalogue, given its extension: saved, and said.
	ctx.selection = Selection::specificationOf(rigplan::SpecificationKind::Lens, "4mm");
	specifications.saveSelected(ctx, folder / "new", rigplan::CatalogueTarget::New);
	CHECK(ctx.message == "Saved lens '4mm' to new.json");
	CHECK_FALSE(ctx.messageIsError);
	REQUIRE(rigplan::readCatalogue(folder / "new.json").specifications);
	CHECK(rp::findLens(*rigplan::readCatalogue(folder / "new.json").specifications, "4mm"));
	// Never over a file that is there, which a save panel will have offered to replace: the sensor it
	// lacks is not added to it.
	ctx.selection = Selection::specificationOf(rigplan::SpecificationKind::Sensor, "hd");
	const std::string saved = bytesOf(folder / "new.json");
	specifications.saveSelected(ctx, folder / "new.json", rigplan::CatalogueTarget::New);
	CHECK(ctx.messageIsError);
	CHECK(bytesOf(folder / "new.json") == saved);

	// To one with a different "hd": nothing written, and the question opens.
	const fs::path file = folder / "catalogue.json";
	writeCatalogue(file);
	const std::string original = bytesOf(file);
	specifications.saveSelected(ctx, file, rigplan::CatalogueTarget::Existing);
	REQUIRE(ctx.savingOut);
	CHECK(ctx.savingOut->clashes == std::vector<std::string>{"sensor 'hd'"});
	CHECK(bytesOf(file) == original);
	CHECK(imgui.frame(modals) == 0);
	CHECK(open);
	CHECK(imgui.frame(modals) == 0);
	CHECK(open); // it stays open, unanswered

	// Cancel writes nothing.
	specifications.answerClash(ctx, rigplan::ClashAnswer::Cancel);
	CHECK_FALSE(ctx.savingOut);
	CHECK(bytesOf(file) == original);
	CHECK(imgui.frame(modals) == 0);
	CHECK_FALSE(open);

	// Keep both saves the plan's under a numbered name, beside the catalogue's.
	specifications.saveSelected(ctx, file, rigplan::CatalogueTarget::Existing);
	REQUIRE(ctx.savingOut);
	specifications.answerClash(ctx, rigplan::ClashAnswer::KeepBoth);
	CHECK(ctx.message == "Saved sensor 'hd' to catalogue.json as 'hd 2'");
	CHECK(width(file, "hd") == 1920);
	CHECK(width(file, "hd 2") == 1280);

	// Replace overwrites the catalogue's, with what the plan had when it was asked: an edit made while
	// the question is open is not what was asked about.
	writeCatalogue(file);
	specifications.saveSelected(ctx, file, rigplan::CatalogueTarget::Existing);
	REQUIRE(ctx.savingOut);
	for (rp::SensorSpecification& s : ctx.document.plan.specifications.sensors)
	{
		if (s.name == "hd")
			s.image.width = 640;
	}
	specifications.answerClash(ctx, rigplan::ClashAnswer::Replace);
	CHECK(ctx.message == "Saved sensor 'hd' to catalogue.json");
	CHECK(width(file, "hd") == 1280);
	CHECK(width(file, "hd 2") == 0);
}

TEST_CASE("Escape is Cancel in each of the menu bar's questions", "[rigplan][panes][catalogue]")
{
	lain::io::data::registerDataCodecs();
	HeadlessImGui imgui;
	Idle idle;
	lain::app::Application app(idle, {"test-rigplan-app", {0, 0, 0}});
	rigplan::AppContext ctx;
	rigplan::MenuBarPane menuBar;
	rigplan::SpecificationsMenu specifications;
	const fs::path folder = lain::testing::scratchDir() / "panes-escape";
	fs::create_directories(folder);
	const fs::path file = folder / "catalogue.json";
	writeCatalogue(file);
	const auto modals = [&]
	{ menuBar.drawModals(ctx, app); };
	// A frame with Escape down, and one with it let go.
	const auto escape = [&]
	{
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		CHECK(imgui.frame(modals) == 0);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		CHECK(imgui.frame(modals) == 0);
	};
	// One frame of the modals, and whether the question `name` is open in it.
	const auto isOpen = [&](const char* name)
	{
		bool open = false;
		const auto probe = [&]
		{
			modals();
			open = ImGui::IsPopupOpen(name);
		};
		CHECK(imgui.frame(probe) == 0);
		return open;
	};

	// The picker: closed, copying nothing.
	const lain::data::Value before = rigplan::snapshot(ctx.document.plan);
	REQUIRE(specifications.beginCopyIn(ctx, file));
	REQUIRE(isOpen("Copy In from Catalogue"));
	escape();
	CHECK_FALSE(ctx.copyingIn);
	CHECK_FALSE(isOpen("Copy In from Catalogue"));
	CHECK(rigplan::snapshot(ctx.document.plan) == before);

	// The clash question: closed, saving nothing.
	const std::string original = bytesOf(file);
	ctx.selection = Selection::specificationOf(rigplan::SpecificationKind::Sensor, "hd");
	specifications.saveSelected(ctx, file, rigplan::CatalogueTarget::Existing);
	REQUIRE(isOpen("Already in the Catalogue"));
	escape();
	CHECK_FALSE(ctx.savingOut);
	CHECK_FALSE(isOpen("Already in the Catalogue"));
	CHECK(bytesOf(file) == original);

	// The unsaved-changes question: closed, the request dropped and the plan kept.
	ctx.document.dirty = true;
	REQUIRE_FALSE(menuBar.request(ctx, app, {rigplan::DocumentRequest::Kind::New, {}}));
	REQUIRE(isOpen("Unsaved changes"));
	escape();
	CHECK_FALSE(ctx.guard.pending());
	CHECK_FALSE(isOpen("Unsaved changes"));
	CHECK(ctx.document.dirty);
}

TEST_CASE("a key or a shortcut edits nothing behind an open question", "[rigplan][panes][catalogue]")
{
	lain::io::data::registerDataCodecs();
	HeadlessImGui imgui;
	Idle idle;
	lain::app::Application app(idle, {"test-rigplan-app", {0, 0, 0}});
	rigplan::AppContext ctx;
	rigplan::MenuBarPane menuBar;
	rigplan::TimelinePane timeline;
	rigplan::SpecificationsMenu specifications;
	const fs::path folder = lain::testing::scratchDir() / "panes-keys-behind";
	fs::create_directories(folder);
	const fs::path file = folder / "catalogue.json";
	writeCatalogue(file);
	ctx.document.plan = editorplan::plan(lain::core::Uri{"/assets/chair.obj"});
	const auto draw = [&]
	{
		bool resetLayout = false;
		menuBar.draw(ctx, app, resetLayout);
		timeline.draw(ctx);
		menuBar.drawModals(ctx, app);
	};
	// A press of `key`, with what the shortcuts call Ctrl held when `ctrl`: on macOS ImGui swaps Cmd and
	// Ctrl as a key comes in, so that is the Cmd key (Super) there.
	const auto press = [&](ImGuiKey key, bool ctrl)
	{
		ImGuiIO& io = ImGui::GetIO();
		const ImGuiKey command = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
		if (ctrl)
			io.AddKeyEvent(command, true);
		io.AddKeyEvent(key, true);
		CHECK(imgui.frame(draw) == 0);
		io.AddKeyEvent(key, false);
		if (ctrl)
			io.AddKeyEvent(command, false);
		CHECK(imgui.frame(draw) == 0);
	};

	ctx.selection = Selection::object(Selected::Box, editorplan::boxId);
	REQUIRE(specifications.beginCopyIn(ctx, file));
	CHECK(imgui.frame(draw) == 0);
	REQUIRE(ctx.copyingIn);
	const lain::data::Value before = rigplan::snapshot(ctx.document.plan);
	for (ImGuiKey key : {ImGuiKey_Delete, ImGuiKey_Backspace, ImGuiKey_K, ImGuiKey_Space})
	{
		INFO("key " << int(key));
		press(key, false);
	}
	for (ImGuiKey key : {ImGuiKey_D, ImGuiKey_Z, ImGuiKey_N})
	{
		INFO("ctrl key " << int(key));
		press(key, true);
	}
	CHECK(rigplan::snapshot(ctx.document.plan) == before);
	CHECK(ctx.selection == Selection::object(Selected::Box, editorplan::boxId));
	CHECK_FALSE(ctx.playback.playing());
	CHECK(ctx.copyingIn); // the picker is still open: none of them answered it

	// With the question gone the same presses act, so it was the question that held them back.
	// The picker is let go of, and closes; a frame later nothing is open.
	ctx.copyingIn.reset();
	CHECK(imgui.frame(draw) == 0);
	bool anyOpen = true;
	const auto probe = [&]
	{
		draw();
		anyOpen = rigplan::popupOpen();
	};
	CHECK(imgui.frame(probe) == 0);
	REQUIRE_FALSE(anyOpen);
	press(ImGuiKey_D, true);
	CHECK(ctx.document.plan.boxes.size() == 2);
	press(ImGuiKey_Delete, false);
	CHECK(ctx.document.plan.boxes.size() == 1);
	press(ImGuiKey_Space, false);
	CHECK(ctx.playback.playing());
}
