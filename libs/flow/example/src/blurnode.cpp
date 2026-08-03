#include "lain/flow/example/blurnode.h"

#include <lain/image/convert.h>	   // convert(img, ColorSpace / AlphaMode)
#include <lain/image/operations.h> // convolve + gaussianKernel

namespace lain::flow::example
{
	BlurNode::BlurNode(int radius, float sigma)
		: Node("Blur")
	{
		m_radius = addParam<int>("radius", radius);
		m_sigma = addParam<float>("sigma", sigma);
		m_in = addInput<image::Image>("image");
		m_out = addOutput<image::Image>("image");
	}

	void BlurNode::compute(NodeEvaluation& evaluation) const
	{
		if (evaluation.input(m_in).empty())
			return;
		const image::Image& src = evaluation.input(m_in).get<image::Image>();
		if (!src.valid())
			return;

		// The upstream gradient/tint emit display bytes with untagged space/alpha. Declare
		// them (sRGB, opaque -> Straight), then blur *correctly*: convolve() is a value-
		// blending op and requires Linear + Premultiplied, so convert in, blur, convert back.
		image::Image in = src;
		// A loaded image may be RGB8/Gray8; normalise to RGBA8 so the premultiply step below
		// operates on a real alpha channel (else it would treat the last colour channel as
		// alpha). The gradient is already RGBA8, so the smoke scene is unchanged.
		if (in.pixelFormat() != image::PixelFormat::RGBA8)
			in = image::convert(in, image::PixelFormat::RGBA8);
		in.setColorSpace(image::ColorSpace::sRGB);
		in.setAlphaMode(image::AlphaMode::Straight);

		const image::Image linear = image::convert(in, image::ColorSpace::Linear);
		const image::Image premul = image::convert(linear, image::AlphaMode::Premultiplied);
		const image::Image blurred =
			image::convolve(premul, image::gaussianKernel(param(m_radius).get<int>(), param(m_sigma).get<float>()));
		const image::Image straight = image::convert(blurred, image::AlphaMode::Straight);

		evaluation.output(m_out).set(image::convert(straight, image::ColorSpace::sRGB));
	}
} // namespace lain::flow::example
