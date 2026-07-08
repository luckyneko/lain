#pragma once

#include <functional>
#include <map>
#include <typeindex>

namespace lain::flow
{
	class Param;
}

namespace flowview
{
	// The adapter's type-keyed param editor registry (ADR-0005): maps a param's declared
	// type to an ImGui editor. flow stays UI-free — a Param is pure data; this is where the
	// gui lives. The widget is chosen by TYPE (an existing type where one fits:
	// std::filesystem::path -> a path field, image::ColorRGBf -> a colour swatch, int/float
	// -> drag, bool -> checkbox, std::string -> text). A custom param type registers its own
	// editor here; a type with none falls back to read-only describe() text.
	class ParamEditors
	{
	public:
		// An editor draws the widget for `param` (labelled by param.name()), writes the new
		// value back on change, and returns true iff the value changed this frame. ImGui is
		// single-threaded — an editor runs only on the main/render thread.
		using Editor = std::function<bool(lain::flow::Param& param)>;

		void add(std::type_index type, Editor editor);

		// Render `param`'s editor (or read-only text if none is registered); returns whether
		// it was edited this frame.
		bool render(lain::flow::Param& param) const;

	private:
		std::map<std::type_index, Editor> m_editors;
	};

	// Register the built-in editors (int, float, bool, std::string, std::filesystem::path,
	// image::ColorRGBf). Called once by the InspectorWindow.
	void registerBuiltinParamEditors(ParamEditors& editors);
} // namespace flowview
