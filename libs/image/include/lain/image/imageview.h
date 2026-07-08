#pragma once

#include "lain/image/image.h"		  // Image (the single owner these view; Image::as<C> in imageview.inl)
#include "lain/image/pixeliterator.h" // PixelIterator — begin()/end()

#include <cstddef>

namespace lain::image
{
	// A non-owning, strided view of an Image's bytes as typed Color pixels — the "iterate
	// over pixels, not bytes" surface. Obtained from Image::as<C>() (full image) or
	// subview() (a window); the Image stays the sole owner, so a view must not outlive it.
	// The row stride (elements to the next row) lets a subview alias a window of a larger
	// image without copying. ConstImageView is the read-only twin.
	template <typename C>
	class ImageView
	{
	public:
		ImageView() = default;
		ImageView(C* data, int width, int height, std::size_t rowStride)
			: m_data(data)
			, m_width(width)
			, m_height(height)
			, m_rowStride(rowStride)
		{
		}

		int width() const { return m_width; }
		int height() const { return m_height; }
		std::size_t rowStride() const { return m_rowStride; }
		bool valid() const { return m_data != nullptr && m_width > 0 && m_height > 0; }

		C& operator()(int x, int y) { return m_data[static_cast<std::size_t>(y) * m_rowStride + x]; }
		const C& operator()(int x, int y) const { return m_data[static_cast<std::size_t>(y) * m_rowStride + x]; }
		C* row(int y) { return m_data + static_cast<std::size_t>(y) * m_rowStride; }
		const C* row(int y) const { return m_data + static_cast<std::size_t>(y) * m_rowStride; }
		C* data() { return m_data; }
		const C* data() const { return m_data; }

		// Row-major pixel iteration (stride-aware) — enables `for (auto& p : view)`.
		using iterator = PixelIterator<C>;
		using const_iterator = PixelIterator<const C>;
		iterator begin() { return {m_data, 0, m_width, m_rowStride}; }
		iterator end() { return {m_data + static_cast<std::size_t>(m_height) * m_rowStride, 0, m_width, m_rowStride}; }
		const_iterator begin() const { return {m_data, 0, m_width, m_rowStride}; }
		const_iterator end() const { return {m_data + static_cast<std::size_t>(m_height) * m_rowStride, 0, m_width, m_rowStride}; }

		// A window aliasing this view's storage (no copy). Rows stay strided by the parent.
		ImageView subview(int x, int y, int w, int h)
		{
			return ImageView(&(*this)(x, y), w, h, m_rowStride);
		}

	private:
		C* m_data = nullptr;
		int m_width = 0;
		int m_height = 0;
		std::size_t m_rowStride = 0;
	};

	template <typename C>
	class ConstImageView
	{
	public:
		ConstImageView() = default;
		ConstImageView(const C* data, int width, int height, std::size_t rowStride)
			: m_data(data)
			, m_width(width)
			, m_height(height)
			, m_rowStride(rowStride)
		{
		}
		// A mutable view converts to a const one.
		ConstImageView(const ImageView<C>& v)
			: m_data(v.data())
			, m_width(v.width())
			, m_height(v.height())
			, m_rowStride(v.rowStride())
		{
		}

		int width() const { return m_width; }
		int height() const { return m_height; }
		std::size_t rowStride() const { return m_rowStride; }
		bool valid() const { return m_data != nullptr && m_width > 0 && m_height > 0; }

		const C& operator()(int x, int y) const { return m_data[static_cast<std::size_t>(y) * m_rowStride + x]; }
		const C* row(int y) const { return m_data + static_cast<std::size_t>(y) * m_rowStride; }
		const C* data() const { return m_data; }

		using const_iterator = PixelIterator<const C>;
		const_iterator begin() const { return {m_data, 0, m_width, m_rowStride}; }
		const_iterator end() const { return {m_data + static_cast<std::size_t>(m_height) * m_rowStride, 0, m_width, m_rowStride}; }

		ConstImageView subview(int x, int y, int w, int h) const
		{
			return ConstImageView(&(*this)(x, y), w, h, m_rowStride);
		}

	private:
		const C* m_data = nullptr;
		int m_width = 0;
		int m_height = 0;
		std::size_t m_rowStride = 0;
	};

} // namespace lain::image

#include "lain/image/details/imageview.inl"
