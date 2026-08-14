#include "lain/flow/example/combinenode.h"

#include <lain/image/convert.h> // normalise to RGBA8, as the other example filters do
#include <lain/log/log.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace lain::flow::example
{
	CombineNode::CombineNode()
		: Node("Combine")
	{
		m_in = addInput<std::vector<image::Image>>("images");
		m_out = addOutput<image::Image>("image");
	}

	void CombineNode::compute(NodeEvaluation& evaluation) const
	{
		const PortValue& slot = evaluation.input(m_in);
		if (slot.empty())
			return; // nothing upstream yet; leave the output as it was

		const std::vector<image::Image>& images = slot.get<std::vector<image::Image>>();
		if (images.empty())
		{
			// An EMPTY collection is a value, not a failure — but there is no meaningful average of
			// nothing, so this produces no image and lets ADR-0007 suppress downstream.
			evaluation.output(m_out).clear();
			return;
		}

		// Normalise once, like TintNode: the accumulate loop below indexes 4 bytes per pixel, so a
		// loaded RGB8 / Gray8 / 16-bit source would overrun it.
		std::vector<image::Image> normalised;
		normalised.reserve(images.size());
		for (const image::Image& image : images)
		{
			if (!image.valid())
			{
				log::warn("flow::example::Combine: an element is not a valid image — no output");
				evaluation.output(m_out).clear();
				return;
			}
			normalised.push_back(image.pixelFormat() == image::PixelFormat::RGBA8
									 ? image
									 : image::convert(image, image::PixelFormat::RGBA8));
		}

		const int width = normalised.front().width();
		const int height = normalised.front().height();
		for (const image::Image& image : normalised)
		{
			// Reject rather than resize: an average across differing extents would silently mean
			// something the caller did not ask for.
			if (image.width() != width || image.height() != height)
			{
				log::warn("flow::example::Combine: elements differ in size — no output");
				evaluation.output(m_out).clear();
				return;
			}
		}

		// Pixelwise mean, accumulated wide so a long collection cannot wrap.
		image::Image out(width, height, image::PixelFormat::RGBA8);
		const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
		const std::size_t count = normalised.size();
		auto* px = out.data();
		for (std::size_t i = 0; i < bytes; ++i)
		{
			std::uint32_t total = 0;
			for (const image::Image& image : normalised)
				total += image.data()[i];
			px[i] = static_cast<std::uint8_t>(total / count);
		}

		evaluation.output(m_out).set(std::move(out));
	}
} // namespace lain::flow::example
