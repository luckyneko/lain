#include "lain/flow/example/gradientnode.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace lain::flow::example
{
	GradientNode::GradientNode(std::uint32_t width, std::uint32_t height)
		: Node("Gradient")
	{
		// Int, not an unsigned type, because Int is the port type a Constant, a Cast or a loop's
		// `index` produces, so the size can be driven from the graph.
		m_width = addInput<int>("width", Default{static_cast<int>(width)});
		m_height = addInput<int>("height", Default{static_cast<int>(height)});
		m_out = addOutput<image::Image>("image");
	}

	void GradientNode::compute(NodeEvaluation& evaluation) const
	{
		const int w = evaluation.input(m_width).get<int>();
		const int h = evaluation.input(m_height).get<int>();
		if (w < 1 || h < 1)
			return; // no image has no pixels; suppress rather than emit an invalid one
		const auto width = static_cast<std::uint32_t>(w);
		const auto height = static_cast<std::uint32_t>(h);

		image::Image img(width, height, image::PixelFormat::RGBA8);
		auto* px = img.data();

		// R ramps across X, G ramps down Y, B constant — a pattern a readback (or the
		// inspector) can check at a known pixel. RGBA8 byte order is [R, G, B, A].
		for (std::uint32_t y = 0; y < height; ++y)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
				px[i + 0] = static_cast<std::uint8_t>(width > 1 ? x * 255 / (width - 1) : 0);
				px[i + 1] = static_cast<std::uint8_t>(height > 1 ? y * 255 / (height - 1) : 0);
				px[i + 2] = 128;
				px[i + 3] = 255;
			}
		}

		evaluation.output(m_out).set(std::move(img));
	}
} // namespace lain::flow::example
