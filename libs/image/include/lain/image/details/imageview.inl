#pragma once

// Template definitions for the typed-view layer (see imageview.h): Image::as<C>() — the checked
// reinterpretation of an Image's bytes as a typed ImageView<C>. C is a Color<F>, so its
// format is C::format. (visit() and the traversal free functions live in traverse.h.)

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lain::image
{
	namespace detail
	{
		// The soundness precondition of viewing an Image's bytes as C: C must be a padding-
		// free, standard-layout, trivially-copyable Color of the matching size. Checked once,
		// here, where the reinterpretation actually happens (so only reinterpreted Colors are
		// checked, and any non-Color C fails on C::format).
		template <typename C>
		constexpr bool isReinterpretable()
		{
			return std::is_standard_layout_v<C> && std::is_trivially_copyable_v<C> && sizeof(C) == descriptor(C::format).bytesPerPixel();
		}
	} // namespace detail

	template <typename C>
	ImageView<C> Image::as()
	{
		static_assert(detail::isReinterpretable<C>(), "Image::as<C>(): C must be a padding-free standard-layout Color");
		assert(m_format == C::format && "Image::as<C>(): runtime PixelFormat does not match C");
		assert(reinterpret_cast<std::uintptr_t>(m_bytes.data()) % alignof(C) == 0 && "Image bytes are underaligned for C");
		if (m_format != C::format)
			return {};
		return ImageView<C>(reinterpret_cast<C*>(m_bytes.data()), m_extent.x, m_extent.y, static_cast<std::size_t>(m_extent.x));
	}

	template <typename C>
	ConstImageView<C> Image::as() const
	{
		static_assert(detail::isReinterpretable<C>(), "Image::as<C>(): C must be a padding-free standard-layout Color");
		assert(m_format == C::format && "Image::as<C>(): runtime PixelFormat does not match C");
		assert(reinterpret_cast<std::uintptr_t>(m_bytes.data()) % alignof(C) == 0 && "Image bytes are underaligned for C");
		if (m_format != C::format)
			return {};
		return ConstImageView<C>(reinterpret_cast<const C*>(m_bytes.data()), m_extent.x, m_extent.y, static_cast<std::size_t>(m_extent.x));
	}
} // namespace lain::image
