#pragma once

#include <lain/gui/gui.h>

namespace lain::gui
{
	// Whether a popup is open: a question, a menu, a combo's list, at any level and whoever opened it.
	// ImGui hands a key, and a shortcut routed globally, to whatever asks about it, whatever is open;
	// so an app that does not ask this acts behind its own question. rigplan's Backspace deleted what
	// was selected behind its Copy In picker, its Cmd+N replaced the plan under the unsaved-changes
	// question, and flowview's Cmd+Z undid behind its own.
	inline bool popupOpen()
	{
		return IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	}

	// Whether a key that means something on its own (Delete, Space, K...) is the app's to act on: not
	// while a text field has the keyboard, and not while a popup is open, which is answered or walked
	// with keys.
	inline bool keysAreFree()
	{
		return !GetIO().WantTextInput && !popupOpen();
	}
} // namespace lain::gui
