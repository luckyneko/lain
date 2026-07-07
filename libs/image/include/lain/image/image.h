#pragma once

#include "lain/image/colorspace.h"	// ColorSpace, AlphaMode (tracked tags)
#include "lain/image/pixelformat.h" // PixelFormat + info() descriptor

#include <lain/math/types.h> // math::Vec2i (extent)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lain::image
{
	// A CPU-side owned raster: extent + PixelFormat + tightly-packed bytes, plus two
	// tracked semantic tags — ColorSpace (the encoding of the values) and AlphaMode
	// (whether color is premultiplied). The lightweight, archimedes/UI-agnostic image
	// foundation: it names no GPU/UI types, so it rides a flow port like any copyable
	// payload. Typed pixel iteration comes from a non-owning ImageView<C> over these bytes
	// (see view.h); Image stays the single owner.
	//
	// The tags are declarative, not derived: a fresh Image is ColorSpace::Unspecified /
	// AlphaMode::Unspecified until a producer or a conversion sets them. Space/alpha-
	// sensitive ops (see the op headers) enforce their required tag; Unspecified asserts.
	class Image
	{
	public:
		Image() = default;
		Image(int width, int height, PixelFormat format = PixelFormat::RGBA8,
			ColorSpace colorSpace = ColorSpace::Unspecified, AlphaMode alphaMode = AlphaMode::Unspecified);

		lain::math::Vec2i extent() const { return m_extent; }
		int width() const { return m_extent.x; }
		int height() const { return m_extent.y; }

		PixelFormat pixelFormat() const { return m_format; }
		constexpr PixelFormatDescriptor descriptor() const { return image::descriptor(m_format); }

		ColorSpace colorSpace() const { return m_colorSpace; }
		AlphaMode alphaMode() const { return m_alphaMode; }
		void setColorSpace(ColorSpace colorSpace) { m_colorSpace = colorSpace; }
		void setAlphaMode(AlphaMode alphaMode) { m_alphaMode = alphaMode; }

		// Tightly-packed pixel bytes (size() == byteSize()). NOTE: the vector type is not
		// meant to be part of the durable contract — a future pooled/mimalloc allocator will
		// route allocation through here; new code should prefer data()/byteSize().
		const std::vector<std::uint8_t>& bytes() const { return m_bytes; }
		std::vector<std::uint8_t>& bytes() { return m_bytes; }
		const std::uint8_t* data() const { return m_bytes.data(); }
		std::uint8_t* data() { return m_bytes.data(); }

		bool valid() const { return m_extent.x > 0 && m_extent.y > 0 && !m_bytes.empty(); }
		std::size_t pixelCount() const { return static_cast<std::size_t>(m_extent.x) * m_extent.y; }
		std::size_t byteSize() const { return pixelCount() * bytesPerPixel(m_format); }

		std::string toString() const; // e.g. "Image 64x64 RGBA8"

		// Bytes per pixel for a format (RGBA8 -> 4) — reads the flyweight descriptor.
		static std::uint32_t bytesPerPixel(PixelFormat format);

	private:
		lain::math::Vec2i m_extent{0, 0};
		PixelFormat m_format{PixelFormat::RGBA8};
		ColorSpace m_colorSpace{ColorSpace::Unspecified};
		AlphaMode m_alphaMode{AlphaMode::Unspecified};
		std::vector<std::uint8_t> m_bytes;
	};
} // namespace lain::image
