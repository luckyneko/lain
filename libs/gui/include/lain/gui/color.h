#pragma once

// The GUI seam between lain's colour types and ImGui/imnodes' packed-colour representation (ImU32).
// Colours stay lain-native (image::Color) until they cross into an ImGui/imnodes call, where this
// converts them — the same role imconfig_lain.h's ImVec2/ImVec4 bridge plays for vectors, but ImU32 is
// a primitive with no class-extra hook, so it needs an explicit function.

#include <lain/image/color.h> // ColorRGBA8

#include <imgui.h> // IM_COL32, ImU32

namespace lain::gui
{
	// Pack a lain RGBA colour into ImGui/imnodes' opaque packed colour. Call it at the toolkit boundary
	// (e.g. imnodes' PushColorStyle), so a palette stays lain-native all the way down to the call.
	inline ImU32 packColor(const lain::image::ColorRGBA8& c)
	{
		return IM_COL32(int(c.r), int(c.g), int(c.b), int(c.a));
	}
} // namespace lain::gui
