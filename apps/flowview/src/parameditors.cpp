#include "parameditors.h"

#include <lain/flow/portvalue.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/image/color.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <typeindex>
#include <utility>

namespace flowview
{
	using namespace lain;

	// The slot's current T, or a default when it's empty (a boundary input not yet set edits from there).
	template <typename T>
	static T currentOr(const flow::PortValue& value, T fallback = T{})
	{
		return value.holds<T>() ? value.get<T>() : fallback;
	}

	// --- built-in editors (named static; each draws + writes, returns edited) -----------

	static bool editInt(const std::string& label, flow::PortValue& value)
	{
		int v = currentOr<int>(value);
		if (gui::DragInt(label.c_str(), &v))
		{
			value.set<int>(v);
			return true;
		}
		return false;
	}

	static bool editFloat(const std::string& label, flow::PortValue& value)
	{
		float v = currentOr<float>(value);
		if (gui::DragFloat(label.c_str(), &v, 0.01f))
		{
			value.set<float>(v);
			return true;
		}
		return false;
	}

	static bool editBool(const std::string& label, flow::PortValue& value)
	{
		bool v = currentOr<bool>(value);
		if (gui::Checkbox(label.c_str(), &v))
		{
			value.set<bool>(v);
			return true;
		}
		return false;
	}

	// A char-buffer InputText (no imgui_stdlib dependency). Sets `changedNow` and copies the
	// buffer into `out` on any per-frame change, so the caller can persist the text live (the
	// buffer is re-seeded from the value each frame, so an un-persisted keystroke would be
	// lost). Returns true only when editing FINISHED — Enter or focus loss — which is the
	// recompute trigger, so a file-loading value doesn't reopen the file every character.
	static bool textField(const char* label, const std::string& current, std::string& out, bool& changedNow)
	{
		char buffer[512];
		std::snprintf(buffer, sizeof(buffer), "%s", current.c_str());
		gui::InputText(label, buffer, sizeof(buffer));
		changedNow = std::string(buffer) != current;
		if (changedNow)
			out = buffer;
		return gui::IsItemDeactivatedAfterEdit();
	}

	static bool editString(const std::string& label, flow::PortValue& value)
	{
		std::string edited;
		bool changed = false;
		const bool committed = textField(label.c_str(), currentOr<std::string>(value), edited, changed);
		if (changed)
			value.set<std::string>(std::move(edited)); // persist live so the text isn't lost
		return committed;								// recompute only on commit
	}

	static bool editPath(const std::string& label, flow::PortValue& value)
	{
		// A text field (type/paste, commit on Enter or focus loss — committing per keystroke would
		// reopen the file each character) beside a Browse… button that opens the native picker.
		// The path TYPE owns this editor, so it slots in without touching any node (ADR-0005).
		std::string edited;
		bool changed = false;
		const bool committed = textField(label.c_str(), currentOr<std::filesystem::path>(value).string(), edited, changed);
		if (changed)
			value.set<std::filesystem::path>(std::filesystem::path(std::move(edited)));

		// Picking a file commits immediately (the same recompute trigger as finishing a text edit).
		// Image filters here because the only path value today is an image input; a per-value filter
		// hint is a future refinement if a non-image path value appears (ADR-0005).
		bool browsed = false;
		gui::PushID(label.c_str());
		gui::SameLine();
		if (gui::Button("Browse..."))
		{
			// The dialog wants a starting DIRECTORY, so drop the filename from the current path
			// (passing a file path breaks the macOS backend — it resolves it as a folder).
			const std::filesystem::path current = currentOr<std::filesystem::path>(value);
			const std::filesystem::path startDir = current.has_filename() ? current.parent_path() : current;
			const auto picked =
				gui::openFile("Open image", startDir, {{"Images", {"*.png", "*.jpg", "*.jpeg", "*.tif", "*.tiff"}}});
			if (picked)
			{
				value.set<std::filesystem::path>(*picked);
				browsed = true;
			}
		}
		gui::PopID();
		return committed || browsed;
	}

	static bool editColorRGBf(const std::string& label, flow::PortValue& value)
	{
		const image::ColorRGBf color = currentOr<image::ColorRGBf>(value);
		float rgb[3] = {color.r, color.g, color.b};
		if (gui::ColorEdit3(label.c_str(), rgb))
		{
			value.set<image::ColorRGBf>(image::ColorRGBf(rgb[0], rgb[1], rgb[2]));
			return true;
		}
		return false;
	}

	// --- registry ----------------------------------------------------------------------

	void ParamEditors::add(std::type_index type, Editor editor)
	{
		m_editors[type] = std::move(editor);
	}

	bool ParamEditors::render(const std::string& label, std::type_index type, flow::PortValue& value) const
	{
		const auto it = m_editors.find(type);
		if (it != m_editors.end())
			return it->second(label, value);

		gui::Text("%s (no editor)", label.c_str()); // unregistered type: read-only note
		return false;
	}

	void registerBuiltinParamEditors(ParamEditors& editors)
	{
		editors.add(typeid(int), &editInt);
		editors.add(typeid(float), &editFloat);
		editors.add(typeid(bool), &editBool);
		editors.add(typeid(std::string), &editString);
		editors.add(typeid(std::filesystem::path), &editPath);
		editors.add(typeid(image::ColorRGBf), &editColorRGBf);
	}
} // namespace flowview
