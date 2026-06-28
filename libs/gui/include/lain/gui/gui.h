#pragma once

// Re-expose Dear ImGui under a lain face — like lain::math over glm — so a consumer
// writes lain::gui::Begin / Button / Image etc. ImGui's free functions live in
// namespace ImGui; the using-directive surfaces them through lain::gui. ImGui's
// types stay global (ImVec2, ImGuiWindowFlags, …) but bridge to lain::math vectors
// (see imconfig_lain.h). The integration seam — backend setup, per-frame render,
// texture preview — is lain::gui::Context (context.h).

#include <imgui.h>

namespace lain::gui
{
	using namespace ImGui;
}
