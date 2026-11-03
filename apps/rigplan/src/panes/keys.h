#pragma once

#include <lain/gui/gui.h>

namespace rigplan
{
	// Whether a popup is open: a question, a menu, a combo's list. ImGui hands a key, and a shortcut routed
	// globally, to whatever asks about it, whatever is open; so without asking this, Backspace in the Copy
	// In picker deleted what was selected behind it, and Cmd+N replaced the plan under it.
	inline bool popupOpen()
	{
		return lain::gui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	}

	// Whether a key that means something on its own (K, F, Delete, Space, W...) is the editor's to act on:
	// not while a text field has the keyboard, and not while a popup is open, which is answered or walked
	// with keys.
	inline bool keysAreFree()
	{
		return !lain::gui::GetIO().WantTextInput && !popupOpen();
	}
} // namespace rigplan
