#include "lain/flow/example/gradientnode.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace lain::flow::example
{
	GradientNode::GradientNode(std::uint32_t width, std::uint32_t height)
		: Node("Gradient")
		, m_width(width)
		, m_height(height)
	{
		m_out = addOutput<image::Image>("image");
	}

	void GradientNode::compute()
	{
		image::Image img(m_width, m_height, image::Format::RGBA8);
		auto& px = img.bytes();

		// R ramps across X, G ramps down Y, B constant — a pattern a readback (or the
		// inspector) can check at a known pixel. RGBA8 byte order is [R, G, B, A].
		for (std::uint32_t y = 0; y < m_height; ++y)
		{
			for (std::uint32_t x = 0; x < m_width; ++x)
			{
				const std::size_t i = (static_cast<std::size_t>(y) * m_width + x) * 4;
				px[i + 0] = static_cast<std::uint8_t>(m_width > 1 ? x * 255 / (m_width - 1) : 0);
				px[i + 1] = static_cast<std::uint8_t>(m_height > 1 ? y * 255 / (m_height - 1) : 0);
				px[i + 2] = 128;
				px[i + 3] = 255;
			}
		}

		output(m_out).set(std::move(img));
	}
} // namespace lain::flow::example
