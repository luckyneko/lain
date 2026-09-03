#include "lain/image/image.h"

#include "lain/image/traverse.h" // detail::colorList (visit table) — for the completeness check

#include <lain/meta/enums.h>

#include <cstring> // memset / memcpy for the pixel buffer

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
		, m_bytes((width > 0 && height > 0) ? static_cast<std::size_t>(width) * height * bytesPerPixel(format) : 0)
	{
		// Zero the pixels — the previous std::vector storage was value-initialised, and some
		// callers rely on a fresh Image being cleared. (Buffer itself leaves bytes untouched.)
		if (!m_bytes.empty())
			std::memset(m_bytes.data(), 0, m_bytes.size());
	}

	Image::Image(const Image& other)
		: m_extent(other.m_extent)
		, m_format(other.m_format)
		, m_colorSpace(other.m_colorSpace)
		, m_alphaMode(other.m_alphaMode)
		, m_bytes(other.m_bytes.size())
	{
		if (!m_bytes.empty())
			std::memcpy(m_bytes.data(), other.m_bytes.data(), m_bytes.size());
	}

	Image& Image::operator=(const Image& other)
	{
		if (this != &other)
		{
			m_extent = other.m_extent;
			m_format = other.m_format;
			m_colorSpace = other.m_colorSpace;
			m_alphaMode = other.m_alphaMode;
			m_bytes = memory::Buffer(other.m_bytes.size());
			if (!m_bytes.empty())
				std::memcpy(m_bytes.data(), other.m_bytes.data(), m_bytes.size());
		}
		return *this;
	}

	std::string Image::toString() const
	{
		// Both tags are named, always — including when they are Unspecified. This string is what
		// a codec's refusal prints (io::image::encode, the video writer's frame check), and those
		// refusals are ABOUT the tags: "cannot encode Image 64x64 RGB8" names nothing the caller
		// can act on, while "... RGB8 Linear Straight" says which conversion to insert.
		return "Image " + std::to_string(m_extent.x) + "x" + std::to_string(m_extent.y) + " " + std::string(lain::meta::enums::name(m_format)) +
			   " " + std::string(lain::meta::enums::name(m_colorSpace)) + " " + std::string(lain::meta::enums::name(m_alphaMode));
	}
} // namespace lain::image
