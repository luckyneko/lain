#pragma once

#include "lain/image/pixelformat.h" // PixelFormat + descriptor()

#include <lain/math/types.h> // math::Vec<N,T> — Color's base

#include <cstddef>

namespace lain::image
{
	// The math::Vec a Color<F> derives from: channel count and channel type read off the
	// format's descriptor, so the format alone determines the layout. The channel type comes
	// from the shared channelTypes list (pixelformat.h), indexed by the enum value — the same
	// list that gives a format its byte size.
	template <PixelFormat F>
	using ColorBase = lain::math::Vec<
		static_cast<math::length_t>(descriptor(F).channelCount()),
		detail::channelTypes::template at<static_cast<std::size_t>(descriptor(F).channelType)>>;

	// A pixel value of a given PixelFormat, as a distinct strong type over its math::Vec.
	// Parameterizing on the format (not on <N,T>) ties a Color 1:1 to a PixelFormat, so the
	// format bridge is intrinsic (Color<F>::format) and the whole system speaks one axis.
	//
	// Deriving from math::Vec (not aliasing) buys a home for cross-format value
	// normalization (u8 0-255 <-> f32 0-1, see convert()), color-only helpers (luminance /
	// saturate), and distinction from a spatial vector. It inherits GLM's arithmetic and
	// .rgba accessors; GLM's operators return the base Vec, so re-wrap a result into a
	// Color explicitly (the Base-converting constructor). Color is a struct to match
	// glm::vec (public inheritance, no encapsulation to add). It adds no data members, so
	// it stays standard-layout + trivially-copyable and padding-free (asserted in
	// Image::as<C>()) — which is what makes an ImageView<C> a sound reinterpretation of bytes.
	template <PixelFormat F>
	struct Color : ColorBase<F>
	{
		using Base = ColorBase<F>;
		using Base::Base;			 // inherit GLM's constructors
		constexpr Color() = default; // ...and keep a trivial default
		constexpr Color(const Base& v)
			: Base(v)
		{
		} // re-wrap a GLM op result

		static constexpr PixelFormat format = F; // the format this Color realizes
	};

	// --- named concretes: {Gray, RGB, RGBA} x {U8, U16, F32}; f == f32 ---
	using ColorGray8 = Color<PixelFormat::Gray8>;
	using ColorGray16 = Color<PixelFormat::Gray16>;
	using ColorGrayf = Color<PixelFormat::Gray32F>;
	using ColorRGB8 = Color<PixelFormat::RGB8>;
	using ColorRGB16 = Color<PixelFormat::RGB16>;
	using ColorRGBf = Color<PixelFormat::RGB32F>;
	using ColorRGBA8 = Color<PixelFormat::RGBA8>;
	using ColorRGBA16 = Color<PixelFormat::RGBA16>;
	using ColorRGBAf = Color<PixelFormat::RGBA32F>;

	// A Color's format / channel count / channel type come straight from the type: the
	// format is the template parameter (Color<F>::format), and the channel count and channel
	// storage type are the descriptor's channelCount() and GLM's value_type. So there is no
	// separate traits struct — descriptor(C::format) + Color<F>::value_type say it all. (That
	// a Color is a sound reinterpretation target — standard-layout, trivially-copyable,
	// padding-free — is asserted at the point of reinterpretation, in Image::as<C>().)
	//
	// The color-value algorithms (luminance / saturate / convert) live in colormath.h, the
	// free-function surface over Color — kept apart from this type header the same way the
	// view traversal lives in traverse.h, not imageview.h.
} // namespace lain::image
