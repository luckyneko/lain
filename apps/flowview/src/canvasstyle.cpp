#include "canvasstyle.h"

#include <lain/image/colormath.h> // convert (HSV -> RGB)
#include <lain/image/image.h>

#include <cstddef>
#include <functional>
#include <utility>

namespace flowview
{
	using namespace lain;

	CanvasStyle::CanvasStyle()
		: m_defaultTitle(55, 55, 66, 255)
		, m_mutedTitle(42, 42, 48, 255)
		, m_mutedBackground(34, 34, 38, 255)
		, m_mutedPin(96, 96, 102, 255)
		, m_mutedLink(70, 70, 78, 255)
	{
	}

	void CanvasStyle::addPortColor(std::type_index type, image::ColorRGBA8 colour)
	{
		m_portColours[type] = colour;
	}

	void CanvasStyle::addNodeColor(std::string key, image::ColorRGBA8 colour)
	{
		m_nodeColours[std::move(key)] = colour;
	}

	image::ColorRGBA8 CanvasStyle::portColor(std::type_index type, std::string_view typeName) const
	{
		const auto it = m_portColours.find(type);
		if (it != m_portColours.end())
			return it->second;
		// Unregistered type: hash its name to a hue for a stable, distinct fallback colour.
		const std::size_t h = std::hash<std::string_view>{}(typeName);
		return image::convert<image::ColorRGBA8>(image::ColorHSVf{static_cast<float>(h % 360u), 0.55f, 0.85f});
	}

	image::ColorRGBA8 CanvasStyle::nodeTitle(const std::string& key) const
	{
		const auto it = m_nodeColours.find(key);
		return it != m_nodeColours.end() ? it->second : m_defaultTitle;
	}

	void registerBuiltinCanvasStyle(CanvasStyle& style)
	{
		// Port (value) colours — the scene payload + the scalars. Keyed by C++ type, so a pin carrying
		// image::Image / int / … resolves regardless of how it was declared.
		style.addPortColor(typeid(image::Image), image::ColorRGBA8(80, 140, 235, 255)); // blue
		style.addPortColor(typeid(int), image::ColorRGBA8(95, 190, 95, 255));		   // green
		style.addPortColor(typeid(float), image::ColorRGBA8(90, 195, 150, 255));		   // teal-green
		style.addPortColor(typeid(bool), image::ColorRGBA8(180, 120, 225, 255));		   // purple
		style.addPortColor(typeid(std::string), image::ColorRGBA8(230, 175, 80, 255));  // amber

		// Node title colours by factory kind — categories are emergent from shared colour (no Category
		// enum). These keys must match scene.cpp's registrations.
		const image::ColorRGBA8 source(46, 86, 120, 255);	// sources: emit a value
		const image::ColorRGBA8 filter(38, 104, 104, 255);	// filters: transform a value
		const image::ColorRGBA8 control(86, 58, 124, 255);	// control flow: gate / merge / select
		const image::ColorRGBA8 boundary(74, 74, 86, 255);	// the graph's I/O
		style.addNodeColor("gradient", source);
		style.addNodeColor("loadimage", source);
		style.addNodeColor("constInt", source);
		style.addNodeColor("constBool", source);
		style.addNodeColor("tint", filter);
		style.addNodeColor("blur", filter);
		style.addNodeColor("gate", control);
		style.addNodeColor("merge", control);
		style.addNodeColor("select", control);
		style.addNodeColor("groupInput", boundary);
		style.addNodeColor("groupOutput", boundary);
	}

} // namespace flowview
