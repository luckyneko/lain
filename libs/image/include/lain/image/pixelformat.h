#pragma once

#include <cstdint>

namespace lain::image
{
	// The color model of a pixel: which channels it carries, in memory order. The
	// *semantic* axis of a PixelFormat (paired with ChannelType). Interleaved only for
	// now; planar / subsampled models (YUV) and reordered layouts (BGRA) arrive later.
	enum class ColorModel
	{
		Gray, // single luminance channel
		RGB,  // red, green, blue
		RGBA, // red, green, blue, alpha
	};

	// The per-channel storage type. The *numeric* axis of a PixelFormat (paired with
	// ColorModel). Values are normalized by convention: U8 [0,255], U16 [0,65535],
	// F32 [0,1] — Color<N,T> owns the cross-type normalization.
	enum class ChannelType
	{
		U8,	 // 8-bit unsigned
		U16, // 16-bit unsigned
		F32, // 32-bit float
	};

	// A pixel's byte layout: ColorModel x ChannelType. The primary runtime handle an
	// Image carries — cheap to switch on and magic_enum-reflectable. Deliberately NOT the
	// color space (sRGB/linear): that is a separate tracked axis (see colorspace.h), since
	// sRGB and linear share a byte layout but not a meaning. Enumerators read
	// model-then-depth; F marks float.
	enum class PixelFormat
	{
		Gray8,
		Gray16,
		Gray32F,
		RGB8,
		RGB16,
		RGB32F,
		RGBA8,
		RGBA16,
		RGBA32F,
	};

	// A format's reflective facts, derived from its two axes. Unlike flow's PortType
	// flyweight (referenced by pointer, so it needs a stable address), a descriptor is
	// pure derivable data with no identity — so it is a plain constexpr value returned by
	// descriptor(PixelFormat). Sizes are computed from (model, channelType), not hardcoded
	// per format, so a new format is one line in descriptor().
	struct PixelFormatDescriptor
	{
		PixelFormat format;		  // the format this describes
		ColorModel model;		  // channel set + order
		ChannelType channelType;  // per-channel storage

		constexpr std::uint8_t channelCount() const // 1 (Gray), 3 (RGB), 4 (RGBA)
		{
			switch (model)
			{
				case ColorModel::Gray:
					return 1;
				case ColorModel::RGB:
					return 3;
				case ColorModel::RGBA:
					return 4;
			}
			return 0;
		}

		constexpr std::uint8_t bytesPerChannel() const // 1 (U8), 2 (U16), 4 (F32)
		{
			switch (channelType)
			{
				case ChannelType::U8:
					return 1;
				case ChannelType::U16:
					return 2;
				case ChannelType::F32:
					return 4;
			}
			return 0;
		}

		constexpr std::uint32_t bytesPerPixel() const
		{
			return static_cast<std::uint32_t>(channelCount()) * bytesPerChannel();
		}

		constexpr bool hasAlpha() const { return model == ColorModel::RGBA; }
	};

	// The descriptor for a format — a single constexpr mapping enum -> {model, channelType}.
	constexpr PixelFormatDescriptor descriptor(PixelFormat format)
	{
		switch (format)
		{
			case PixelFormat::Gray8:
				return {format, ColorModel::Gray, ChannelType::U8};
			case PixelFormat::Gray16:
				return {format, ColorModel::Gray, ChannelType::U16};
			case PixelFormat::Gray32F:
				return {format, ColorModel::Gray, ChannelType::F32};
			case PixelFormat::RGB8:
				return {format, ColorModel::RGB, ChannelType::U8};
			case PixelFormat::RGB16:
				return {format, ColorModel::RGB, ChannelType::U16};
			case PixelFormat::RGB32F:
				return {format, ColorModel::RGB, ChannelType::F32};
			case PixelFormat::RGBA8:
				return {format, ColorModel::RGBA, ChannelType::U8};
			case PixelFormat::RGBA16:
				return {format, ColorModel::RGBA, ChannelType::U16};
			case PixelFormat::RGBA32F:
				return {format, ColorModel::RGBA, ChannelType::F32};
		}
		return {format, ColorModel::RGBA, ChannelType::U8}; // unreachable: all formats handled
	}
} // namespace lain::image
