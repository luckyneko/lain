#include "lain/flow/example/imagedifferencenode.h"

#include <lain/image/convert.h> // normalise to RGBA8, as the other example filters do
#include <lain/log/log.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace lain::flow::example
{
	ImageDifferenceNode::ImageDifferenceNode()
		: Node("ImageDifference")
	{
		m_a = addInput<image::Image>("a");
		m_b = addInput<image::Image>("b");
		m_difference = addOutput<float>("difference");
	}

	void ImageDifferenceNode::compute(NodeEvaluation& evaluation) const
	{
		if (evaluation.input(m_a).empty() || evaluation.input(m_b).empty())
			return; // nothing upstream yet; leave the output as it was

		const image::Image& srcA = evaluation.input(m_a).get<image::Image>();
		const image::Image& srcB = evaluation.input(m_b).get<image::Image>();
		if (!srcA.valid() || !srcB.valid())
		{
			log::warn("flow::example::ImageDifference: an input is not a valid image — no output");
			evaluation.output(m_difference).clear();
			return;
		}

		// Normalise once, for the reason CombineNode does: the byte loop below assumes 4 bytes per
		// pixel, so an RGB8 / Gray8 / 16-bit source would be measured against the wrong strides.
		const image::Image a = srcA.pixelFormat() == image::PixelFormat::RGBA8 ? srcA : image::convert(srcA, image::PixelFormat::RGBA8);
		const image::Image b = srcB.pixelFormat() == image::PixelFormat::RGBA8 ? srcB : image::convert(srcB, image::PixelFormat::RGBA8);

		if (a.width() != b.width() || a.height() != b.height())
		{
			// Reject rather than resize: a difference across differing extents would silently mean
			// something the caller did not ask for.
			log::warn("flow::example::ImageDifference: the inputs differ in size — no output");
			evaluation.output(m_difference).clear();
			return;
		}

		// Mean absolute difference over every byte, accumulated wide so a large image cannot wrap,
		// then normalised by 255 so the answer is a fraction a threshold can be written against
		// without knowing the bit depth.
		const std::size_t bytes = static_cast<std::size_t>(a.width()) * a.height() * 4;
		if (bytes == 0)
		{
			evaluation.output(m_difference).set(0.0f); // no pixels: nothing differs, which is a value
			return;
		}
		std::uint64_t total = 0;
		const std::uint8_t* pa = a.data();
		const std::uint8_t* pb = b.data();
		for (std::size_t i = 0; i < bytes; ++i)
			total += static_cast<std::uint64_t>(std::abs(static_cast<int>(pa[i]) - static_cast<int>(pb[i])));

		evaluation.output(m_difference).set(static_cast<float>(static_cast<double>(total) / (static_cast<double>(bytes) * 255.0)));
	}
} // namespace lain::flow::example
