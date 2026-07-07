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
		// radius/sigma of the Gaussian kernel (e.g. 2 / 1.5 gives a soft 5x5 blur).
		explicit BlurNode(int radius = 2, float sigma = 1.5f);

		// Index of the lain::image::Image input / output ports.
		PortIndex inputPort() const { return m_in; }
		PortIndex imagePort() const { return m_out; }

		void compute() override;

	private:
		int m_radius;
		float m_sigma;
		PortIndex m_in;
		PortIndex m_out;
	};
} // namespace lain::flow::example
