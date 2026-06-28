#pragma once

// Dear ImGui user config (wired via IMGUI_USER_CONFIG in libs/gui/CMakeLists.txt).
// Bridges ImGui's own ImVec2/ImVec4 to lain::math vectors so a lain::gui consumer
// passes Vec2f/Vec4f straight into ImGui calls and reads them back — no per-call
// casts. Included by imgui.h before the ImVec types are defined.

#include <lain/math/types.h>

#define IM_VEC2_CLASS_EXTRA            \
	ImVec2(const lain::math::Vec2f& v) \
		: x(v.x)                       \
		, y(v.y)                       \
	{                                  \
	}                                  \
	operator lain::math::Vec2f() const { return {x, y}; }

#define IM_VEC4_CLASS_EXTRA            \
	ImVec4(const lain::math::Vec4f& v) \
		: x(v.x)                       \
		, y(v.y)                       \
		, z(v.z)                       \
		, w(v.w)                       \
	{                                  \
	}                                  \
	operator lain::math::Vec4f() const { return {x, y, z, w}; }
