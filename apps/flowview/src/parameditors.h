#pragma once

#include <functional>
#include <map>
#include <typeindex>

namespace lain::flow
{
	class PortValue;
}

namespace flowview
{
	// The adapter's type-keyed value editor registry (ADR-0005): maps a declared type to an ImGui
	// editor over a PortValue slot. flow stays UI-free — this is where the gui lives. The widget is
	// chosen by TYPE (an existing type where one fits: std::filesystem::path -> a path field,
	// image::ColorRGBf -> a colour swatch, int/float -> drag, bool -> checkbox, std::string -> text).
	// A custom type registers its own editor; a type with none falls back to a read-only note. Because
	// it edits a bare (label, type, PortValue) slot, it serves BOTH node params and boundary-input
	// values — a Param exposes its value() PortValue; a boundary input its published pin value.
	class ParamEditors
	{
	public:
		// An editor draws the widget for `value` under `label`, writes the new value back on change, and
		// returns true iff it changed this frame. An empty slot (a boundary input not yet set) edits from
		// the type's default. ImGui is single-threaded — an editor runs only on the main/render thread.
		using Editor = std::function<bool(const std::string& label, lain::flow::PortValue& value)>;

		void add(std::type_index type, Editor editor);

		// Render the editor registered for `type` over `value` (or a read-only note if none); returns
		// whether it was edited this frame.
		bool render(const std::string& label, std::type_index type, lain::flow::PortValue& value) const;

	private:
		std::map<std::type_index, Editor> m_editors;
	};

	// Register the built-in editors (int, float, bool, std::string, std::filesystem::path,
	// image::ColorRGBf). Called once by the MainWindow.
	void registerBuiltinParamEditors(ParamEditors& editors);
} // namespace flowview
