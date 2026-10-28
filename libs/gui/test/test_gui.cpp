// Tests for lain::gui's foundation: ImGui is re-exposed under lain::gui, and the
// ImVec2/ImVec4 <-> lain::math bridge (imconfig_lain.h) converts both ways. No
// window or device needed: GetVersion is a constant, the conversions are pure, and the
// cases that draw run a CPU-only ImGui frame (imnodes, the status bar, a frame-resolved
// texture's stand-ins, ImGuizmo). The windowed Context seam is test_texture.cpp's.

#include "lain/gui/dock.h"
#include "lain/gui/enums.h"
#include "lain/gui/frametexture.h"
#include "lain/gui/gizmo.h"
#include "lain/gui/gui.h"
#include "lain/gui/nodes.h"

#include <lain/math/types.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

TEST_CASE("ImGui is re-exposed under lain::gui", "[gui]")
{
	REQUIRE(std::string(lain::gui::GetVersion()) == std::string(IMGUI_VERSION));
}

TEST_CASE("ImVec2 bridges to lain::math::Vec2f both ways", "[gui]")
{
	const lain::math::Vec2f v{3.0f, 4.0f};
	const ImVec2 im = v; // Vec2f -> ImVec2
	REQUIRE(im.x == 3.0f);
	REQUIRE(im.y == 4.0f);

	const lain::math::Vec2f back = im; // ImVec2 -> Vec2f
	REQUIRE(back.x == 3.0f);
	REQUIRE(back.y == 4.0f);
}

TEST_CASE("ImVec4 bridges to lain::math::Vec4f both ways", "[gui]")
{
	const lain::math::Vec4f v{1.0f, 2.0f, 3.0f, 4.0f};
	const ImVec4 im = v;
	REQUIRE(im.z == 3.0f);
	REQUIRE(im.w == 4.0f);

	const lain::math::Vec4f back = im;
	REQUIRE(back.x == 1.0f);
	REQUIRE(back.w == 4.0f);
}

TEST_CASE("enumCombo builds headlessly over an enum", "[gui]")
{
	// A real (CPU-only) ImGui frame: no backend / GPU, just enough state for NewFrame.
	// The combo stays closed (no interaction), so it reports no change and leaves the
	// value alone — this smokes the integration; interactive selection is visual.
	enum class Pick
	{
		A,
		B,
		C,
	};

	ImGuiContext* ctx = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // nothing read or written: a saved window size would carry from one test process to the next
	io.DisplaySize = ImVec2(200.0f, 200.0f);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height); // build the atlas so NewFrame's assert passes

	ImGui::NewFrame();
	Pick pick = Pick::B;
	const bool changed = lain::gui::enumCombo("pick", pick);
	ImGui::Render(); // finalize draw data (CPU only)

	REQUIRE_FALSE(changed);
	REQUIRE(pick == Pick::B);

	ImGui::DestroyContext(ctx);
}

TEST_CASE("an imnodes node with an empty body is still drawable", "[gui][nodes]")
{
	// Regression guard. imnodes positions a node's CONTENT with ImGui::SetCursorPos, and ImGui
	// asserts ("Code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries")
	// when the body then submits no item at all. flowview hit this the moment group nodes arrived:
	// a freshly added group has NO pins — its ports mirror an inner boundary that starts empty — and
	// unlike the boundary nodes it has no "+" buttons to accidentally satisfy the rule. The canvas
	// submits a placeholder for any pin-less node; this pins that rule down with no driver in sight.
	//
	// The assertion is ImGui's own: if the placeholder below is removed, this test aborts rather than
	// failing politely — which is exactly the signal wanted, since that is what the app would do.
	ImGuiContext* imguiCtx = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // nothing read or written: a saved window size would carry from one test process to the next
	io.DisplaySize = ImVec2(640.0f, 480.0f);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	io.Fonts->SetTexID(static_cast<ImTextureID>(1)); // imnodes draws, so the atlas needs an id

	ImNodesContext* nodesCtx = lain::gui::nodes::CreateContext();

	// Two frames: freshly-created state can carry the first one regardless.
	for (int frame = 0; frame < 2; ++frame)
	{
		ImGui::NewFrame();
		ImGui::Begin("Graph");
		lain::gui::nodes::BeginNodeEditor();
		lain::gui::nodes::BeginNode(1);
		lain::gui::nodes::BeginNodeTitleBar();
		ImGui::TextUnformatted("Group");
		lain::gui::nodes::EndNodeTitleBar();
		ImGui::TextDisabled("(empty - double-click)"); // the placeholder the canvas submits
		lain::gui::nodes::EndNode();
		lain::gui::nodes::EndNodeEditor();
		ImGui::End();
		ImGui::Render();
	}

	SUCCEED("a pin-less node drew without tripping ImGui's cursor-bounds assert");

	lain::gui::nodes::DestroyContext(nodesCtx);
	ImGui::DestroyContext(imguiCtx);
}

TEST_CASE("the status bar takes its height out of the viewport's work area", "[gui][dock]")
{
	// What lets a host draw a bar under a full-viewport dockspace: the side bar shrinks the work area
	// the dockspace fills, one frame later (the menu bar's arrangement, on the other edge). If it did
	// not, the dockspace would cover it.
	ImGuiContext* imguiCtx = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // nothing read or written: a saved window size would carry from one test process to the next
	io.DisplaySize = ImVec2(640.0f, 480.0f);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	for (int frame = 0; frame < 2; ++frame)
	{
		ImGui::NewFrame();
		if (lain::gui::beginStatusBar())
		{
			ImGui::TextUnformatted("Live");
			lain::gui::endStatusBar();
		}
		ImGui::Render();
	}

	ImGui::NewFrame();
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	REQUIRE(viewport->WorkSize.y < io.DisplaySize.y);
	REQUIRE(viewport->WorkPos.y == 0.0f); // taken from the BOTTOM: nothing moved the top edge
	ImGui::Render();

	ImGui::DestroyContext(imguiCtx);
}

// A callback that draws nothing, to stand in a draw list as ImGui's callbacks do.
static void drawsNothing(const ImDrawList*, const ImDrawCmd*)
{
}

TEST_CASE("a frame-resolved texture's stand-in is replaced, and nothing else is", "[gui]")
{
	// Context::render runs this rule on every frame's draw data, after the frame's pre-pass, so that
	// what a pane shows is the texture there is when the frame is recorded (frametexture.h). Here it is
	// run on a real (CPU-only) ImGui frame with made-up ids, so it needs no device.
	ImGuiContext* imguiCtx = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // nothing read or written: a saved window size would carry from one test process to the next
	io.DisplaySize = ImVec2(640.0f, 480.0f);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	io.Fonts->SetTexID(static_cast<ImTextureID>(1));

	const ImTextureID standIn = 0x1000;
	const ImTextureID other = 0x2000;
	const ImTextureID resolved = 0x3000;

	// Two frames, the rule run on the second: ImGui hides a window in the frame that creates it.
	for (int frame = 0; frame < 2; ++frame)
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("pane");
		lain::gui::Image(ImTextureRef(standIn), lain::math::Vec2f{16.0f, 16.0f});
		ImGui::TextUnformatted("the font atlas between");
		lain::gui::Image(ImTextureRef(other), lain::math::Vec2f{16.0f, 16.0f});
		lain::gui::Image(ImTextureRef(standIn), lain::math::Vec2f{16.0f, 16.0f}); // after another texture: a command of its own
		ImDrawList* list = ImGui::GetWindowDrawList();
		list->PushTexture(ImTextureRef(standIn));
		list->AddCallback(drawsNothing, nullptr); // a callback naming the stand-in is never offered
		list->PopTexture();
		ImGui::End();
		ImGui::Render();
	}
	ImDrawData& data = *ImGui::GetDrawData();

	// Every command's texture id and whether it carries a callback, in order.
	struct Command
	{
		ImTextureID texture;
		bool callback;
	};
	auto commands = [&]()
	{
		std::vector<Command> all;
		for (const ImDrawList* l : data.CmdLists)
		{
			for (const ImDrawCmd& cmd : l->CmdBuffer)
				all.push_back({cmd.TexRef._TexData ? ImTextureID_Invalid : cmd.TexRef._TexID, cmd.UserCallback != nullptr});
		}
		return all;
	};
	const std::vector<Command> before = commands();

	std::vector<ImTextureID> offered;
	const std::size_t replaced = lain::gui::detail::resolveStandIns(data,
																	[&](ImTextureID id) -> std::optional<ImTextureID>
																	{
																		offered.push_back(id);
																		if (id == standIn)
																			return resolved;
																		return std::nullopt;
																	});
	const std::vector<Command> after = commands();

	REQUIRE(before.size() == after.size());
	std::size_t standIns = 0;
	for (std::size_t i = 0; i < before.size(); ++i)
	{
		INFO("command " << i);
		if (before[i].texture == standIn && !before[i].callback)
		{
			++standIns;
			CHECK(after[i].texture == resolved);
		}
		else
			CHECK(after[i].texture == before[i].texture); // the atlas, the other texture, the callback
	}
	CHECK(standIns == 2);
	CHECK(replaced == 2);
	CHECK(std::count(offered.begin(), offered.end(), other) == 1);
	CHECK(std::count(offered.begin(), offered.end(), standIn) == 2); // never the callback's

	ImGui::DestroyContext(imguiCtx);
}

TEST_CASE("ImGuizmo runs a whole frame headlessly through lain::gui::gizmo", "[gui][gizmo]")
{
	// No backend and no device: a gizmo is geometry ImGui draws into a window's draw list, so a frame
	// that shows one runs on the CPU alone. Nothing presses the mouse, so nothing is moved or in use.
	ImGuiContext* imguiCtx = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // nothing read or written: a saved window size would carry from one test process to the next
	io.DisplaySize = ImVec2(640.0f, 480.0f);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	io.Fonts->SetTexID(static_cast<ImTextureID>(1));

	// OpenGL's conventions, which ImGuizmo's are: a camera 5 units back along +z looking down -z, and a
	// 60 degree perspective, column-major.
	const lain::math::Mat4f view = lain::math::translate(lain::math::Mat4f{1.0f}, lain::math::Vec3f{0.0f, 0.0f, -5.0f});
	const lain::math::Mat4f projection = lain::math::perspective(lain::math::radians(60.0f), 640.0f / 480.0f, 0.1f, 100.0f);
	lain::math::Mat4f matrix{1.0f};

	for (int frame = 0; frame < 2; ++frame)
	{
		INFO("frame " << frame);
		ImGui::NewFrame();
		lain::gui::gizmo::BeginFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("viewport");
		lain::gui::gizmo::SetDrawlist();
		lain::gui::gizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
		const int verticesBefore = ImGui::GetWindowDrawList()->VtxBuffer.Size;
		const bool moved = lain::gui::gizmo::Manipulate(&view[0][0], &projection[0][0], lain::gui::gizmo::TRANSLATE,
														lain::gui::gizmo::WORLD, &matrix[0][0]);
		const int verticesAfter = ImGui::GetWindowDrawList()->VtxBuffer.Size;
		ImGui::End();
		ImGui::Render();

		CHECK(verticesAfter > verticesBefore); // its handles were drawn into the pane
		CHECK_FALSE(moved);
		CHECK_FALSE(lain::gui::gizmo::IsUsing());
	}
	CHECK(matrix == lain::math::Mat4f{1.0f});

	ImGui::DestroyContext(imguiCtx);
}
