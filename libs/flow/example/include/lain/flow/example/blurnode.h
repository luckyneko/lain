#pragma once

#include <lain/flow/node.h>
#include <lain/image/image.h>

namespace lain::flow::example
{
	// A CPU transform node that Gaussian-blurs its input image — the example that dogfoods
	// lain::image's operation catalog through a graph edge. Because convolve() is a
	// value-blending op, compute() runs the correct tracked-tag workflow: declare the
	// incoming display bytes as sRGB / Straight, convert to Linear + Premultiplied, convolve,
	// then convert back. Pure CPU; no GPU device involved.
	class BlurNode : public Node
	{
	public:
		// radius/sigma of the Gaussian kernel (e.g. 2 / 1.5 gives a soft 5x5 blur) — the
		// ctor args seed editable params of the same name.
		explicit BlurNode(int radius = 2, float sigma = 1.5f);

		void compute() override;

	private:
		PortId m_radius; // "radius" param (int)
		PortId m_sigma;	 // "sigma" param (float)
		PortId m_in;
		PortId m_out;
	};
} // namespace lain::flow::example
