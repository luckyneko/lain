#pragma once

#include <lain/gui/gui.h> // ImGui re-exposed as lain::gui::
#include <lain/meta/enums.h>

#include <string>

namespace lain::gui
{
	// A combo box over an enum's values, each labelled by its name (lain::meta). Edits
	// `value` in place and returns true on the frame the selection changes. Like every
	// lain::gui widget, this is ImGui — call it on the main/render thread only.
	template <typename E>
	bool enumCombo(const char* label, E& value)
	{
		bool changed = false;
		const std::string current(lain::meta::enums::name(value));
		if (BeginCombo(label, current.c_str()))
		{
			for (const auto& [option, optionName] : lain::meta::enums::entries<E>())
			{
				const bool selected = (option == value);
				if (Selectable(std::string(optionName).c_str(), selected))
				{
					value = option;
					changed = true;
				}
				if (selected)
					SetItemDefaultFocus();
			}
			EndCombo();
		}
		return changed;
	}
} // namespace lain::gui
