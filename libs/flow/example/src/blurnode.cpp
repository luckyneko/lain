#include "lain/flow/example/blurnode.h"

#include <lain/image/convert.h>	   // convert(img, ColorSpace / AlphaMode)
#include <lain/image/operations.h> // convolve + gaussianKernel

namespace lain::flow::example
{
	BlurNode::BlurNode(int radius, float sigma)
		: Node("Blur")
	{
		// The DATA input leads, and the settings follow it. That ordering is not cosmetic: an input's
		// position is how tests and hand-built graphs address it (`connect(src, 0, blur, 0)`), so
		// declaring the settings first would silently retarget every such edge onto `radius`.
		// Serialized documents are unaffected either way — edges are addressed by port NAME.
		m_in = addInput<image::Image>("image");
		// Inputs WITH DEFAULTS, not plain params: a blur strength that varies per element is exactly
		// what a map wants to express (soften each frame a little more than the last, say), and a
		// param cannot do that — every element evaluates one definition. Unwired, they behave as
		// before, so an existing document loads and runs identically.
		m_radius = addInput<int>("radius", Default{radius});
		m_sigma = addInput<float>("sigma", Default{sigma});
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

		// DECLARE only what is undeclared — ConvertNode's rule (see its `assume` param), and the
		// reason it is a rule: this used to assign sRGB unconditionally, so a BT709 video frame
		// was linearised with the wrong curve and came back out relabelled sRGB. A tag the
		// upstream stated is a fact, and a convenience default must not overwrite one.
		const image::ColorSpace assumed =
			in.colorSpace() == image::ColorSpace::Unspecified ? image::ColorSpace::sRGB : in.colorSpace();
		const image::AlphaMode assumedAlpha =
			in.alphaMode() == image::AlphaMode::Unspecified ? image::AlphaMode::Straight : in.alphaMode();
		in.setColorSpace(assumed);
		in.setAlphaMode(assumedAlpha);

		const image::Image linear = image::convert(in, image::ColorSpace::Linear);
		const image::Image premul = image::convert(linear, image::AlphaMode::Premultiplied);
		const image::Image blurred =
			image::convolve(premul, image::gaussianKernel(evaluation.input(m_radius).get<int>(), evaluation.input(m_sigma).get<float>()));

		// Back to whatever the input was in, on both axes — a filter changes the pixels, not the
		// space or the association they are expressed in.
		const image::Image restored = image::convert(blurred, assumedAlpha);
		evaluation.output(m_out).set(image::convert(restored, assumed));
	}
} // namespace lain::flow::example
