// Tests for lain::gui's foundation: ImGui is re-exposed under lain::gui, and the
// ImVec2/ImVec4 <-> lain::math bridge (imconfig_lain.h) converts both ways. No
// ImGui context / window needed — GetVersion is a constant and the conversions are
// pure. The windowed Context seam is verified by flowview.

#include "lain/gui/enums.h"
#include "lain/gui/gui.h"
#include "lain/gui/nodes.h"

#include <lain/math/types.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

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
