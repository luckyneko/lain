#include "lain/image/image.h"

#include <lain/meta/enums.h>

namespace lain::image
{
	std::uint32_t Image::bytesPerPixel(Format format)
	{
		switch (format)
		{
			case Format::RGBA8:
				return 4;
		}
		return 0;
	}

	Image::Image(int width, int height, Format format)
		: m_extent(width, height)
		, m_format(format)
		, m_bytes((width > 0 && height > 0) ? static_cast<std::size_t>(width) * height * bytesPerPixel(format) : 0, 0)
	{
	}

	std::string Image::toString() const
	{
		return "Image " + std::to_string(m_extent.x) + "x" + std::to_string(m_extent.y) + " " + std::string(lain::meta::enums::name(m_format));
	}
} // namespace lain::image
