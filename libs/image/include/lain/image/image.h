#pragma once

#include "lain/image/colorspace.h"	// ColorSpace, AlphaMode (tracked tags)
#include "lain/image/pixelformat.h" // PixelFormat + info() descriptor

#include <lain/math/types.h>	// math::Vec2i (extent)
#include <lain/memory/buffer.h> // memory::Buffer (the pixel storage)

#include <cstddef>
#include <cstdint>
#include <string>

namespace lain::image
{
	template <typename C>
	class ImageView; // non-owning typed view (view.h)
	template <typename C>
	class ConstImageView;

	// A CPU-side owned raster: extent + PixelFormat + tightly-packed bytes, plus two
	// tracked semantic tags — ColorSpace (the encoding of the values) and AlphaMode
	// (whether color is premultiplied). The lightweight, archimedes/UI-agnostic image
	// foundation: it names no GPU/UI types, so it rides a flow port like any copyable
	// payload. Typed pixel iteration comes from a non-owning ImageView<C> over these bytes
	// (see imageview.h); Image stays the single owner.
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

		// Image stays COPYABLE — it rides flow's std::any PortValue (which requires copyable)
		// and convert()/ops return it by value. Its storage (memory::Buffer) is move-only, so
		// the copy is a hand-written deep copy (a fresh Buffer + a byte copy); the move is the
		// natural Buffer move.
		Image(const Image& other);
		Image& operator=(const Image& other);
		Image(Image&&) noexcept = default;
		Image& operator=(Image&&) noexcept = default;
		~Image() = default;

		lain::math::Vec2i extent() const { return m_extent; }
		int width() const { return m_extent.x; }
		int height() const { return m_extent.y; }

		PixelFormat pixelFormat() const { return m_format; }
		constexpr PixelFormatDescriptor descriptor() const { return image::descriptor(m_format); }

		ColorSpace colorSpace() const { return m_colorSpace; }
		AlphaMode alphaMode() const { return m_alphaMode; }
		void setColorSpace(ColorSpace colorSpace) { m_colorSpace = colorSpace; }
		void setAlphaMode(AlphaMode alphaMode) { m_alphaMode = alphaMode; }

		// The tightly-packed pixel bytes (byteSize() of them). Storage is a memory::Buffer
		// (aligned, routed through the memory::alloc seam, so a future pool serves Image for
		// free); access is the raw pointer + size, not the container, so that stays swappable.
		// The Buffer holds std::byte; callers get uint8_t here — the reinterpret lives once
		// here rather than at every call site. Typed pixel access is Image::as<C>() (imageview.h).
		const std::uint8_t* data() const { return reinterpret_cast<const std::uint8_t*>(m_bytes.data()); }
		std::uint8_t* data() { return reinterpret_cast<std::uint8_t*>(m_bytes.data()); }

		// A typed, non-owning view of these bytes as Color C, for pixel-wise iteration.
		// The runtime PixelFormat must match C::format (asserts; returns an invalid view in
		// release). Defined in imageview.h — include it to use. See image::visit (traverse.h) to
		// dispatch on the runtime format without naming C.
		template <typename C>
		ImageView<C> as();
		template <typename C>
		ConstImageView<C> as() const;

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
		lain::memory::Buffer m_bytes;
	};
} // namespace lain::image
