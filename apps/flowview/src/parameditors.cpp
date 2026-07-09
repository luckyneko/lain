#include "parameditors.h"

#include <lain/flow/param.h>
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

	// --- built-in editors (named static; each draws + writes, returns edited) -----------

	static bool editInt(flow::Param& param)
	{
		int value = param.get<int>();
		if (gui::DragInt(param.name().c_str(), &value))
		{
			param.set<int>(value);
			return true;
		}
		return false;
	}

	static bool editFloat(flow::Param& param)
	{
		float value = param.get<float>();
		if (gui::DragFloat(param.name().c_str(), &value, 0.01f))
		{
			param.set<float>(value);
			return true;
		}
		return false;
	}

	static bool editBool(flow::Param& param)
	{
		bool value = param.get<bool>();
		if (gui::Checkbox(param.name().c_str(), &value))
		{
			param.set<bool>(value);
			return true;
		}
		return false;
	}

	// A char-buffer InputText (no imgui_stdlib dependency). Sets `changedNow` and copies the
	// buffer into `out` on any per-frame change, so the caller can persist the text live (the
	// buffer is re-seeded from the param each frame, so an un-persisted keystroke would be
	// lost). Returns true only when editing FINISHED — Enter or focus loss — which is the
	// recompute trigger, so a file-loading param doesn't reopen the file every character.
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

	static bool editString(flow::Param& param)
	{
		std::string edited;
		bool changed = false;
		const bool committed = textField(param.name().c_str(), param.get<std::string>(), edited, changed);
		if (changed)
			param.set<std::string>(std::move(edited)); // persist live so the text isn't lost
		return committed;							   // recompute only on commit
	}

	static bool editPath(flow::Param& param)
	{
		// A text field (type/paste, commit on Enter or focus loss — committing per keystroke would
		// reopen the file each character) beside a Browse… button that opens the native picker.
		// The path TYPE owns this editor, so it slots in without touching any node (ADR-0005).
		std::string edited;
		bool changed = false;
		const bool committed =
			textField(param.name().c_str(), param.get<std::filesystem::path>().string(), edited, changed);
		if (changed)
			param.set<std::filesystem::path>(std::filesystem::path(std::move(edited)));

		// Picking a file commits immediately (the same recompute trigger as finishing a text edit).
		// Image filters here because the only path param today is an image input; a per-param
		// filter hint is a future refinement if a non-image path param appears (ADR-0005).
		bool browsed = false;
		gui::PushID(param.name().c_str());
		gui::SameLine();
		if (gui::Button("Browse..."))
		{
			// The dialog wants a starting DIRECTORY, so drop the filename from the current path
			// (passing a file path breaks the macOS backend — it resolves it as a folder).
			const std::filesystem::path current = param.get<std::filesystem::path>();
			const std::filesystem::path startDir = current.has_filename() ? current.parent_path() : current;
			const auto picked =
				gui::openFile("Open image", startDir, {{"Images", {"*.png", "*.jpg", "*.jpeg", "*.tif", "*.tiff"}}});
			if (picked)
			{
				param.set<std::filesystem::path>(*picked);
				browsed = true;
			}
		}
		gui::PopID();
		return committed || browsed;
	}

	static bool editColorRGBf(flow::Param& param)
	{
		const image::ColorRGBf color = param.get<image::ColorRGBf>();
		float rgb[3] = {color.r, color.g, color.b};
		if (gui::ColorEdit3(param.name().c_str(), rgb))
		{
			param.set<image::ColorRGBf>(image::ColorRGBf(rgb[0], rgb[1], rgb[2]));
			return true;
		}
		return false;
	}

	// --- registry ----------------------------------------------------------------------

	void ParamEditors::add(std::type_index type, Editor editor)
	{
		m_editors[type] = std::move(editor);
	}

	bool ParamEditors::render(flow::Param& param) const
	{
		const auto it = m_editors.find(param.type());
		if (it != m_editors.end())
			return it->second(param);

		// No editor for this type: show it read-only (the describe() text pathway).
		gui::Text("%s: %s", param.name().c_str(), param.describe().c_str());
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
