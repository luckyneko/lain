#pragma once

#include <lain/math/types.h> // math::Vec2i (extent)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lain::image
{
	// Pixel format. Drives the byte layout of an Image. Only RGBA8 is implemented today,
	// but this enum is the extension point for the packed / planar / subsampled formats to
	// come (RGB, YUV, YUYV, LAB) and for image::convert() between them (future work).
	enum class Format
	{
		RGBA8, // 8-bit R, G, B, A, interleaved
	};

	// A CPU-side owned raster: extent + pixel Format + tightly-packed bytes. The
	// lightweight, archimedes/UI-agnostic image foundation — color-space conversions live
	// here later, and a sibling camera-geometry library (intrinsics / distortion) will
	// build on it. A plain copyable value type, so it rides a flow port like any payload.
	class Image
	{
	public:
		Image() = default;
		Image(int width, int height, Format format = Format::RGBA8);

		lain::math::Vec2i extent() const { return m_extent; }
		int width() const { return m_extent.x; }
		int height() const { return m_extent.y; }
		Format format() const { return m_format; }

		// Tightly-packed pixel bytes (size() == byteSize()).
		const std::vector<std::uint8_t>& bytes() const { return m_bytes; }
		std::vector<std::uint8_t>& bytes() { return m_bytes; }

		bool valid() const { return m_extent.x > 0 && m_extent.y > 0 && !m_bytes.empty(); }
		std::size_t pixelCount() const { return static_cast<std::size_t>(m_extent.x) * m_extent.y; }
		std::size_t byteSize() const { return pixelCount() * bytesPerPixel(m_format); }

		std::string toString() const; // e.g. "Image 64x64 RGBA8"

		// Bytes per pixel for a format (RGBA8 -> 4).
		static std::uint32_t bytesPerPixel(Format format);

	private:
		lain::math::Vec2i m_extent{0, 0};
		Format m_format{Format::RGBA8};
		std::vector<std::uint8_t> m_bytes;
	};
} // namespace lain::image
