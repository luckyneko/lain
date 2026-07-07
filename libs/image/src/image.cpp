#include "lain/image/image.h"

#include "lain/image/traverse.h" // detail::colorList (visit table) — for the completeness check

#include <lain/meta/enums.h>

namespace lain::image
{
	// The two enum-indexed type tables must cover their whole enum (their internal order is
	// guarded reorder-proof at their definitions). Checked here, the one TU where magic_enum
	// is already in scope, so the widely-included headers stay magic_enum-free.
	static_assert(detail::channelTypes::size == lain::meta::enums::count<ChannelType>(),
		"channelTypes must have one entry per ChannelType");
	static_assert(detail::colorList::size == lain::meta::enums::count<PixelFormat>(),
		"colorList must have one Color per PixelFormat");

	std::uint32_t Image::bytesPerPixel(PixelFormat format)
	{
		return image::descriptor(format).bytesPerPixel();
	}

	Image::Image(int width, int height, PixelFormat format, ColorSpace colorSpace, AlphaMode alphaMode)
		: m_extent(width, height)
		, m_format(format)
		, m_colorSpace(colorSpace)
		, m_alphaMode(alphaMode)
		, m_bytes((width > 0 && height > 0) ? static_cast<std::size_t>(width) * height * bytesPerPixel(format) : 0, 0)
	{
	}

	std::string Image::toString() const
	{
		return "Image " + std::to_string(m_extent.x) + "x" + std::to_string(m_extent.y) + " " + std::string(lain::meta::enums::name(m_format));
	}
} // namespace lain::image
