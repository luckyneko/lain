#pragma once

#include <lain/image/color.h> // ColorRGBA8 — the palette's lain-native currency

#include <map>
#include <string>
#include <string_view>
#include <typeindex>

namespace flowview
{
	// The canvas' visual language (UI pass, slice A): type-keyed pin/link colours, kind-keyed node
	// title colours, and the muted tones a suppressed node + its dead edges fall back to. A single home
	// so the draw loop stays thin and a future theme.json / colour editor has one lain-native load
	// target. The palette speaks image::ColorRGBA8 end-to-end (RGBA to match imnodes' target, alpha in
	// hand); the packed ImU32 imnodes wants is produced ONLY by packColor() at the imnodes boundary (the
	// draw loop's PushColorStyle calls) — no colour is packed in the middle. Tuned for the dark canvas
	// (StyleColorsDark); a light theme is a future concern.
	class CanvasStyle
	{
	public:
		CanvasStyle(); // seeds the muted / default tones, so an unpopulated style still renders

		void addPortColor(std::type_index type, lain::image::ColorRGBA8 colour);
		void addNodeColor(std::string key, lain::image::ColorRGBA8 colour);

		// A pin/link colour for a value type: the registered colour, else a deterministic hash of
		// `typeName` (a stable, distinct fallback so an unregistered static-port type is still legible).
		lain::image::ColorRGBA8 portColor(std::type_index type, std::string_view typeName) const;
		// A node title colour for a factory kind key: the registered colour, else a neutral default.
		lain::image::ColorRGBA8 nodeTitle(const std::string& key) const;

		lain::image::ColorRGBA8 mutedTitle() const { return m_mutedTitle; }
		lain::image::ColorRGBA8 mutedBackground() const { return m_mutedBackground; }
		lain::image::ColorRGBA8 mutedPin() const { return m_mutedPin; }
		lain::image::ColorRGBA8 mutedLink() const { return m_mutedLink; }

		// The title-bar accent a SELECTED node wears (pushed as ImNodesCol_TitleBarSelected) — a bright
		// tone distinct from every kind colour, so selection reads at a glance.
		lain::image::ColorRGBA8 selection() const { return m_selection; }

		// The canvas' read-only watermark. A FOREGROUND tone, unlike the muted* colours above — those
		// are backgrounds (mutedTitle is near-black, and reads as nothing at all when used as text).
		// Amber rather than red: this is a restriction, not an error.
		lain::image::ColorRGBA8 readOnlyMark() const { return m_readOnlyMark; }

	private:
		std::map<std::type_index, lain::image::ColorRGBA8> m_portColours;
		std::map<std::string, lain::image::ColorRGBA8> m_nodeColours;
		lain::image::ColorRGBA8 m_defaultTitle;
		lain::image::ColorRGBA8 m_mutedTitle;
		lain::image::ColorRGBA8 m_mutedBackground;
		lain::image::ColorRGBA8 m_mutedPin;
		lain::image::ColorRGBA8 m_mutedLink;
		lain::image::ColorRGBA8 m_selection;
		lain::image::ColorRGBA8 m_readOnlyMark;
	};

	// Populate the built-in palette: the payload + scalar port colours and the node-kind title colours.
	// The kind keys must match those scene.cpp registers with the factory.
	void registerBuiltinCanvasStyle(CanvasStyle& style);
} // namespace flowview
