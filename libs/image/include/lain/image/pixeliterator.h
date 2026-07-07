#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace lain::image
{
	// A forward iterator over a strided grid of pixels, in row-major order. It steps x across
	// a row then jumps by the row stride — so range-for is correct even over a subview window
	// (whose rows are not contiguous in the parent). Returned by ImageView::begin()/end() (as
	// the member iterator / const_iterator aliases); P is the pixel type C or const C.
	template <typename P>
	class PixelIterator
	{
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = std::remove_const_t<P>;
		using difference_type = std::ptrdiff_t;
		using pointer = P*;
		using reference = P&;

		PixelIterator() = default;
		PixelIterator(P* row, int x, int width, std::size_t rowStride)
			: m_row(row)
			, m_x(x)
			, m_width(width)
			, m_rowStride(rowStride)
		{
		}

		reference operator*() const { return m_row[m_x]; }
		pointer operator->() const { return m_row + m_x; }

		PixelIterator& operator++()
		{
			if (++m_x == m_width)
			{
				m_x = 0;
				m_row += m_rowStride;
			}
			return *this;
		}
		PixelIterator operator++(int)
		{
			PixelIterator t = *this;
			++*this;
			return t;
		}

		bool operator==(const PixelIterator& o) const { return m_row == o.m_row && m_x == o.m_x; }
		bool operator!=(const PixelIterator& o) const { return !(*this == o); }

	private:
		P* m_row = nullptr;
		int m_x = 0;
		int m_width = 0;
		std::size_t m_rowStride = 0;
	};
} // namespace lain::image
