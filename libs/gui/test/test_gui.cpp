// Tests for lain::gui's foundation: ImGui is re-exposed under lain::gui, and the
// ImVec2/ImVec4 <-> lain::math bridge (imconfig_lain.h) converts both ways. No
// ImGui context / window needed — GetVersion is a constant and the conversions are
// pure. The windowed Context seam is verified by flowview.

#include "lain/gui/enums.h"
#include "lain/gui/gui.h"

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
