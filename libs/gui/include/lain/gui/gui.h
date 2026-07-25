#pragma once

// Re-expose Dear ImGui under a lain face — like lain::math over glm — so a consumer
// writes lain::gui::Begin / Button / Image etc. ImGui's free functions live in
// namespace ImGui; the using-directive surfaces them through lain::gui. ImGui's
// types stay global (ImVec2, ImGuiWindowFlags, …) but bridge to lain::math vectors
// (see imconfig_lain.h). The integration seam — backend setup, per-frame render,
// texture preview — is lain::gui::Context (context.h).

#include <imgui.h>
// The official std::string InputText wrappers (misc/cpp). They add overloads into namespace ImGui,
// so the using-directive below surfaces lain::gui::InputText(label, std::string*) for free — a
// std::string-native text surface, no caller-managed char[] edit buffer (which would also truncate).
#include <misc/cpp/imgui_stdlib.h>

namespace lain::gui
{
	using namespace ImGui;
}
