#pragma once

// Definitions for image::visit (see traverse.h) plus its private dispatch table. The table
// is an implementation detail of visit, so it lives here rather than in a public header.

#include <cstddef>
#include <utility>

namespace lain::image
{
	namespace detail
	{
		// The Color of each PixelFormat, indexed by the enum value — the table visit()
		// dispatches over (via meta::TypeList::visitAt). Order must match the PixelFormat
		// enum; the static_assert verifies each slot's Color reports its own position's
		// format (reorder-proof). Completeness (all formats present) is asserted against the
		// enum count in image.cpp, where magic_enum is already in scope.
		using colorList = lain::meta::TypeList<
			ColorGray8, ColorGray16, ColorGrayf,
			ColorRGB8, ColorRGB16, ColorRGBf,
			ColorRGBA8, ColorRGBA16, ColorRGBAf,
			ColorGrayAlpha8, ColorGrayAlpha16, ColorGrayAlphaf>;

		template <std::size_t... Is>
		constexpr bool colorListMatchesFormats(std::index_sequence<Is...>)
		{
			return (... && (colorList::at<Is>::format == static_cast<PixelFormat>(Is)));
		}
		static_assert(
			colorListMatchesFormats(std::make_index_sequence<colorList::size>{}),
			"colorList must list each PixelFormat's Color in enum order");
	} // namespace detail

	template <typename F>
	void visit(Image& img, F&& fn)
	{
		detail::colorList::visitAt(static_cast<std::size_t>(img.pixelFormat()),
								   [&](auto tag)
								   { fn(img.as<typename decltype(tag)::type>()); });
	}

	template <typename F>
	void visit(const Image& img, F&& fn)
	{
		detail::colorList::visitAt(static_cast<std::size_t>(img.pixelFormat()),
								   [&](auto tag)
								   { fn(img.as<typename decltype(tag)::type>()); });
	}
} // namespace lain::image
