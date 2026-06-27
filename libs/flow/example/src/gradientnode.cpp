#include <lain/flow/example/gradientnode.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lain::flow::example
{
	GradientNode::GradientNode(acm::Device device, acm::Extent2D extent)
	    : Node("Gradient"), m_device(device), m_extent(extent)
	{
		m_out = addOutput<acm::Texture>("texture");
		m_texture = m_device.createTexture(acm::Format::R8G8B8A8_Unorm, m_extent);
	}

	void GradientNode::compute()
	{
		const uint32_t w = m_extent.width;
		const uint32_t h = m_extent.height;

		// R ramps across X, G ramps down Y, B constant — a pattern a readback (or
		// the inspector) can check at a known pixel. R8G8B8A8_Unorm memory order
		// is [R, G, B, A].
		std::vector<uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
		for (uint32_t y = 0; y < h; ++y)
		{
			for (uint32_t x = 0; x < w; ++x)
			{
				const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
				pixels[i + 0] = static_cast<uint8_t>(w > 1 ? x * 255 / (w - 1) : 0);
				pixels[i + 1] = static_cast<uint8_t>(h > 1 ? y * 255 / (h - 1) : 0);
				pixels[i + 2] = 128;
				pixels[i + 3] = 255;
			}
		}

		m_texture.upload(pixels.data(), pixels.size());
		output(m_out).set(m_texture);
	}
}
