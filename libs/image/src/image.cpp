#include "lain/image/image.h"

#include <lain/meta/enums.h>

namespace lain::image
{
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
