// flowview's menu bar and node canvas, drawn in a headless ImGui frame (no backend, no device), as
// rigplan's panes are: the keys a frame is handed go through the production panes, and what they did is
// read back from the document and the AppContext they share.
//
// WORK.md's ninth known defect: every shortcut was a gui::Shortcut routed globally, which ImGui hands to
// whoever asks, whatever popup is open. Measured here before the fix, with the unsaved-changes question
// asking about a Quit: Cmd+Z undid and Cmd+Shift+Z redid behind it, Cmd+G grouped the selection,
// Cmd+Shift+G refused out loud, and Cmd+N then Cmd+O replaced the question being asked with an Open. The
// canvas's Delete held there, since the question takes the focus the canvas waits for; but its own
// add-node palette leaves the canvas active, and Delete deleted the selection behind that. Now the menu
// bar acts on no shortcut while a popup is open (gui::popupOpen), and Delete waits for the keys to be
// free (gui::keysAreFree).

#include "appcontext.h"
#include "flowviewapp.h"
#include "panes/graphpane.h"
#include "panes/menubar.h"
#include "scene.h" // registerExampleNodes — the palette's own factory, for a node to act on

#include <lain/app/application.h>
#include <lain/app/applicationdelegate.h>
#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/gui/gui.h>
#include <lain/gui/keys.h>
#include <lain/gui/nodes.h>

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h> // GImGui->ErrorCountCurrentFrame: the errors ImGui recovered from this frame

#include <cstdint>
#include <functional>
#include <memory>

using namespace lain;

// An ImGui context and the canvas's imnodes context for a test, with the font atlas built so NewFrame
// runs, and nothing read or written.
struct HeadlessImGui
{
	ImGuiContext* context = ImGui::CreateContext();
	ImNodesContext* nodes = nullptr;

	HeadlessImGui()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1280.0f, 800.0f);
		io.DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		io.Fonts->SetTexID(static_cast<ImTextureID>(1)); // imnodes draws, so the atlas needs an id
		nodes = gui::nodes::CreateContext();
		gui::nodes::SetImGuiContext(context);
	}
	~HeadlessImGui()
	{
		gui::nodes::DestroyContext(nodes);
		ImGui::DestroyContext(context);
	}

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
struct Idle : app::ApplicationDelegate
{
	bool onStart(app::Application&) override { return true; }
};

// flowview's window without its device: the canvas, the menu bar and its question, drawn in the order
// MainWindow draws them, over a document holding one tint, which the canvas has selected. The document
// is unsaved, with a step to undo, so every question and every edit has something to act on.
struct Editor
{
	HeadlessImGui imgui;
	Idle idle;
	app::Application application{idle, {"test-flowview", {0, 0, 0}}};
	flowview::FlowviewApp app;
	flowview::AppContext ctx;
	flowview::MenuBarPane menuBar;
	flowview::GraphPane canvas;
	flow::NodeId tint;
	int edits = 0; // frames in which a pane reported an edit

	Editor()
	{
		core::Factory<flow::Node> factory;
		flowview::registerExampleNodes(factory, 64);
		auto document = std::make_unique<flow::Graph>();
		tint = document->add(factory.create("tint"));
		app.replaceGraph(std::move(document));
		ctx.app = &app;
		ctx.dirty = true;
		// Two stand-in states to step between: a test reads only that an undo was asked for, and the
		// restore it asks for refuses them as no document (logged).
		ctx.undo.reset(data::Value(std::int64_t{1}));
		ctx.undo.push(data::Value(std::int64_t{2}));
		canvas.init();

		// The tint drawn, then selected in the canvas, which has the focus.
		CHECK(frame() == 0);
		CHECK(frame() == 0);
		gui::nodes::SelectNode(ctx.canvas.node(tint));
		focusCanvas();
	}

	void draw()
	{
		bool edited = false;
		bool resetLayout = false;
		// Placed, so a test knows where the canvas is on screen.
		gui::SetNextWindowPos(ImVec2(0.0f, 40.0f));
		gui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
		canvas.draw(ctx, app.graph(), &app.graph(), app.published(), edited);
		menuBar.draw(ctx, app.graph(), application, edited, resetLayout);
		menuBar.drawConfirmModal(ctx, app.graph(), application);
		edits += edited ? 1 : 0;
	}

	// One frame of the window, and how many errors ImGui recovered from in it.
	int frame()
	{
		return imgui.frame([this]
						   { draw(); });
	}

	// A frame in which the canvas is given the focus, as a user working in it has it.
	void focusCanvas()
	{
		CHECK(imgui.frame([&]
						  {
			gui::SetWindowFocus("Graph");
			draw(); }) == 0);
	}

	// Whether a popup is open, asked once every pane has drawn a frame.
	bool anyPopupOpen()
	{
		bool open = false;
		CHECK(imgui.frame([&]
						  {
			draw();
			open = gui::popupOpen(); }) == 0);
		return open;
	}

	// A press of `key`, with what the shortcuts call Ctrl held when `command`, and Shift when `shift`: on
	// macOS ImGui swaps Cmd and Ctrl as a key comes in, so that is the Cmd key (Super) there.
	void press(ImGuiKey key, bool command, bool shift)
	{
		ImGuiIO& io = ImGui::GetIO();
		const ImGuiKey commandKey = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
		if (command)
			io.AddKeyEvent(commandKey, true);
		if (shift)
			io.AddKeyEvent(ImGuiMod_Shift, true);
		io.AddKeyEvent(key, true);
		CHECK(frame() == 0);
		io.AddKeyEvent(key, false);
		if (shift)
			io.AddKeyEvent(ImGuiMod_Shift, false);
		if (command)
			io.AddKeyEvent(commandKey, false);
		CHECK(frame() == 0);
	}
};

TEST_CASE("a shortcut or a key edits nothing behind the unsaved-changes question", "[flowview][panes]")
{
	Editor editor;
	flowview::AppContext& ctx = editor.ctx;
	REQUIRE_FALSE(editor.anyPopupOpen());

	// Quit, with unsaved work: the question is asked, and held open.
	REQUIRE_FALSE(editor.menuBar.requestQuit(ctx, editor.application));
	REQUIRE(editor.anyPopupOpen());
	REQUIRE(ctx.guard.pending() != nullptr);
	REQUIRE(ctx.guard.pending()->kind == flowview::DocumentSwap::Quit);

	// Behind it, nothing a key or a shortcut means may happen. S and Shift+S are left out: a Save behind
	// the question would open a native panel, which a test cannot answer. Run, Run Selection and Stop
	// leave nothing a frame can read back, so they show only that they are not refused out loud.
	editor.press(ImGuiKey_Delete, false, false); // the canvas deletes the selection
	editor.press(ImGuiKey_Z, true, false);		 // Undo
	editor.press(ImGuiKey_Z, true, true);		 // Redo
	editor.press(ImGuiKey_G, true, false);		 // Group the selection
	editor.press(ImGuiKey_G, true, true);		 // Ungroup (the tint is no group, so it would say why)
	editor.press(ImGuiKey_N, true, false);		 // New, which would replace the question being asked
	editor.press(ImGuiKey_O, true, false);		 // Open, likewise
	editor.press(ImGuiKey_Enter, true, false);	 // Run
	editor.press(ImGuiKey_Enter, true, true);	 // Run Selection
	editor.press(ImGuiKey_Period, true, false);	 // Stop
	CHECK(editor.edits == 0);					 // no edit: no delete, no group
	CHECK(editor.app.graph().contains(editor.tint));
	CHECK_FALSE(ctx.undo.canRedo());		  // nothing was undone
	CHECK_FALSE(ctx.loadRequested);			  // nothing was restored or opened
	CHECK_FALSE(ctx.recentIssue.has_value()); // nothing was refused out loud either
	REQUIRE(ctx.guard.pending() != nullptr);  // the question is the one asked...
	CHECK(ctx.guard.pending()->kind == flowview::DocumentSwap::Quit);
	CHECK(editor.anyPopupOpen()); // ...and still open: none of them answered it

	// Escape is its Cancel: the question goes, and the guard holds nothing. A frame later nothing is open.
	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
	CHECK(editor.frame() == 0);
	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
	CHECK(editor.frame() == 0);
	CHECK(ctx.guard.pending() == nullptr);
	REQUIRE_FALSE(editor.anyPopupOpen());

	// With the question gone the same presses act, so it was the question that held them back.
	editor.press(ImGuiKey_G, true, true); // Ungroup, refused out loud: the tint is no group
	CHECK(ctx.recentIssue.has_value());
	editor.press(ImGuiKey_Z, true, false); // Undo
	CHECK(ctx.undo.canRedo());
	CHECK(ctx.loadRequested);
	editor.focusCanvas();
	editor.press(ImGuiKey_Delete, false, false); // the canvas deletes the tint
	CHECK(editor.edits == 1);
	CHECK_FALSE(editor.app.graph().contains(editor.tint));
	editor.press(ImGuiKey_N, true, false); // New, held by the guard: the work is still unsaved
	REQUIRE(ctx.guard.pending() != nullptr);
	CHECK(ctx.guard.pending()->kind == flowview::DocumentSwap::New);
}

TEST_CASE("the canvas deletes nothing behind its own add-node palette", "[flowview][panes]")
{
	// The palette is a popup of the canvas's own window, so it leaves the canvas active, which is all
	// Delete used to wait for. A right-click on empty canvas opens it, at a point inside the placed window.
	Editor editor;
	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(600.0f, 500.0f);
	editor.focusCanvas();
	io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
	CHECK(editor.frame() == 0);
	io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
	CHECK(editor.frame() == 0);
	REQUIRE(editor.anyPopupOpen());

	editor.press(ImGuiKey_Delete, false, false);
	CHECK(editor.edits == 0);
	CHECK(editor.app.graph().contains(editor.tint));
	CHECK(editor.anyPopupOpen()); // the palette is still there to pick from

	// With the palette closed by a click outside every window, which leaves the canvas's selection alone,
	// the same press deletes: it was the palette that held it back.
	io.AddMousePosEvent(1000.0f, 700.0f);
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	CHECK(editor.frame() == 0);
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	CHECK(editor.frame() == 0);
	REQUIRE_FALSE(editor.anyPopupOpen());
	editor.focusCanvas();
	editor.press(ImGuiKey_Delete, false, false);
	CHECK(editor.edits == 1);
	CHECK_FALSE(editor.app.graph().contains(editor.tint));
}
