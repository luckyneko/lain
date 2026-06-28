#pragma once

#include <array>
#include <cstddef>

#include <lain/math/types.h>

namespace lain::app
{
	// Backend-decoupled key/button identifiers — the Application maps GLFW codes
	// onto these, so a consumer never includes or names GLFW. Contiguous for array
	// indexing; extend freely (Count stays last).
	enum class Key
	{
		Unknown = 0,
		A, B, C, D, E, F, G, H, I, J, K, L, M,
		N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
		Space, Enter, Escape, Tab, Backspace, Delete,
		Left, Right, Up, Down,
		LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
		Count
	};

	enum class MouseButton
	{
		Left = 0,
		Right,
		Middle,
		Count
	};

	// A per-frame input snapshot the Application fills (from the focused window) and
	// passes to ApplicationDelegate::onUpdate — main thread only. down() is the held
	// state; pressed()/released() are this-frame edges; cursor/scroll deltas are the
	// movement since the previous frame. Plain data + query helpers (the App writes
	// the arrays directly — no friend needed).
	struct InputState
	{
		lain::math::Vec2f cursor{ 0.0f, 0.0f };
		lain::math::Vec2f cursorDelta{ 0.0f, 0.0f };
		lain::math::Vec2f scroll{ 0.0f, 0.0f };

		std::array<bool, static_cast<size_t>(Key::Count)> keys{};
		std::array<bool, static_cast<size_t>(Key::Count)> prevKeys{};
		std::array<bool, static_cast<size_t>(MouseButton::Count)> buttons{};
		std::array<bool, static_cast<size_t>(MouseButton::Count)> prevButtons{};

		bool down(Key k) const { return keys[static_cast<size_t>(k)]; }
		bool pressed(Key k) const
		{
			const size_t i = static_cast<size_t>(k);
			return keys[i] && !prevKeys[i];
		}
		bool released(Key k) const
		{
			const size_t i = static_cast<size_t>(k);
			return !keys[i] && prevKeys[i];
		}

		bool down(MouseButton b) const { return buttons[static_cast<size_t>(b)]; }
		bool pressed(MouseButton b) const
		{
			const size_t i = static_cast<size_t>(b);
			return buttons[i] && !prevButtons[i];
		}
		bool released(MouseButton b) const
		{
			const size_t i = static_cast<size_t>(b);
			return !buttons[i] && prevButtons[i];
		}
	};
}
