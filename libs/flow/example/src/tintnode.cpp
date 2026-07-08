#include "lain/flow/example/tintnode.h"

#include <lain/image/color.h>	// image::ColorRGBf — the tint param type
#include <lain/image/convert.h> // normalise a loaded image to RGBA8

#include <cstddef>
#include <cstdint>
#include <utility>

namespace lain::flow::example
{
	TintNode::TintNode(float tintR, float tintG, float tintB)
		: Node("Tint")
	{
		// image::ColorRGBf (an existing RGB-float type) as the param, so the adapter renders
		// a colour swatch — no bespoke "Color" type needed (ADR-0005: prefer existing types).
		m_tint = addParam<image::ColorRGBf>("tint", image::ColorRGBf(tintR, tintG, tintB));
		m_in = addInput<image::Image>("image");
		m_out = addOutput<image::Image>("image");
	}

	void TintNode::compute()
	{
		// No upstream image yet -> nothing to emit (leave the output slot as it was).
		if (!input(m_in).ready())
			return;

		image::Image src = input(m_in).get<image::Image>();
		if (!src.valid())
			return;

		// The per-pixel loop below assumes RGBA8 (4 bytes/px); a loaded image may be RGB8 /
		// Gray8 / 16-bit, so normalise first — indexing i*4+3 on an RGB8 buffer overruns it.
		// (The gradient is already RGBA8, so the smoke scene is unchanged.)
		if (src.pixelFormat() != image::PixelFormat::RGBA8)
			src = image::convert(src, image::PixelFormat::RGBA8);

		// out = clamp(in * factor), alpha preserved. RGBA8 byte order is [R, G, B, A].
		const auto tint = [](std::uint8_t v, float f) -> std::uint8_t
		{
			const float scaled = static_cast<float>(v) * f;
			return static_cast<std::uint8_t>(scaled > 255.0f ? 255.0f : scaled);
		};

		const image::ColorRGBf factor = param(m_tint).get<image::ColorRGBf>();

		image::Image out(src.width(), src.height(), src.pixelFormat());
		const auto* in = src.data();
		auto* px = out.data();
		const std::size_t count = src.pixelCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			px[i * 4 + 0] = tint(in[i * 4 + 0], factor.r);
			px[i * 4 + 1] = tint(in[i * 4 + 1], factor.g);
			px[i * 4 + 2] = tint(in[i * 4 + 2], factor.b);
			px[i * 4 + 3] = in[i * 4 + 3];
		}

		output(m_out).set(std::move(out));
	}
} // namespace lain::flow::example
