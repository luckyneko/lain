#include "parameditors.h"

#include <lain/flow/param.h>
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

	// A char-buffer InputText (no imgui_stdlib dependency); the value is small config text.
	static bool textField(const char* label, const std::string& current, std::string& out)
	{
		char buffer[512];
		std::snprintf(buffer, sizeof(buffer), "%s", current.c_str());
		if (gui::InputText(label, buffer, sizeof(buffer)))
		{
			out = buffer;
			return true;
		}
		return false;
	}

	static bool editString(flow::Param& param)
	{
		std::string edited;
		if (textField(param.name().c_str(), param.get<std::string>(), edited))
		{
			param.set<std::string>(std::move(edited));
			return true;
		}
		return false;
	}

	static bool editPath(flow::Param& param)
	{
		// A plain path text field for now — type/paste the path. A "Browse…" dialog is a
		// deferred enhancement (needs a file-dialog dep); the path TYPE keeps its own editor
		// so the dialog slots in here without touching any node (ADR-0005).
		std::string edited;
		if (textField(param.name().c_str(), param.get<std::filesystem::path>().string(), edited))
		{
			param.set<std::filesystem::path>(std::filesystem::path(std::move(edited)));
			return true;
		}
		return false;
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
