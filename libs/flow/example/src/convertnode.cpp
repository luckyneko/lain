#include "lain/flow/example/convertnode.h"

#include <lain/image/convert.h>

#include <utility>

namespace lain::flow::example
{
	ConvertNode::ConvertNode()
		: Node("Convert")
	{
		// sRGB as the assumed default, which is the convention BlurNode already applies to an
		// untagged input; BT709 as the target default, because the one thing this node exists to
		// feed is a video encoder.
		m_assume = addParam<image::ColorSpace>("assume", image::ColorSpace::sRGB);
		m_format = addParam<image::PixelFormat>("pixelFormat", image::PixelFormat::RGB8);
		m_colorSpace = addParam<image::ColorSpace>("colorSpace", image::ColorSpace::BT709);
		m_in = addInput<image::Image>("image");
		m_out = addOutput<image::Image>("image");
	}

	void ConvertNode::compute(NodeEvaluation& evaluation) const
	{
		if (evaluation.input(m_in).empty())
			return;

		image::Image src = evaluation.input(m_in).get<image::Image>();
		if (!src.valid())
		{
			// PROPAGATE the invalidity rather than returning and leaving whatever was in the output
			// slot. A render binds a new frame position and re-runs, so a stale valid image here
			// reads downstream as "this frame rendered" — and the missing-frame policy, which is
			// the only thing standing between a caller and a silently wrong video, never fires.
			// An invalid Image is how this engine already spells "no value for this frame".
			evaluation.output(m_out).set(image::Image{});
			return;
		}

		// DECLARE, do not convert: an untagged image has no source curve to convert from, and
		// image::convert refuses Unspecified for exactly that reason. A tagged image is left alone
		// — a target-shaped param must not be able to overwrite a stated fact by accident.
		if (src.colorSpace() == image::ColorSpace::Unspecified)
			src.setColorSpace(param(m_assume).get<image::ColorSpace>());

		// Format first, matching the order TintNode normalises in.
		const image::PixelFormat format = param(m_format).get<image::PixelFormat>();
		if (src.pixelFormat() != format)
			src = image::convert(src, format);

		const image::ColorSpace space = param(m_colorSpace).get<image::ColorSpace>();
		if (src.valid() && src.colorSpace() != space)
			src = image::convert(src, space);

		// A failed conversion is the same story: emit the invalid image rather than the last good
		// one, so the failure reaches the caller instead of being papered over.
		evaluation.output(m_out).set(std::move(src));
	}
} // namespace lain::flow::example
