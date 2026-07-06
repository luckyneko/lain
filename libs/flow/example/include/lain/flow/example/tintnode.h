#pragma once

#include <lain/flow/node.h>
#include <lain/image/image.h>

namespace lain::flow::example
{
	// A CPU transform node: reads a lain::image::Image on its input, multiplies each
	// pixel's RGB by a fixed tint (alpha preserved), and emits the result on its output.
	// The connectable counterpart to GradientNode — a graph edge carries an image through
	// it. Pure CPU; no GPU device involved.
	class TintNode : public Node
	{
	public:
		// tintR/G/B are per-channel multipliers (e.g. 1.0, 0.5, 0.5 keeps red, halves
		// green and blue). Values above 1 are clamped at 255 per channel.
		TintNode(float tintR, float tintG, float tintB);

		// Index of the lain::image::Image input / output ports.
		PortIndex inputPort() const { return m_in; }
		PortIndex imagePort() const { return m_out; }

		void compute() override;

	private:
		float m_tintR;
		float m_tintG;
		float m_tintB;
		PortIndex m_in;
		PortIndex m_out;
	};
} // namespace lain::flow::example
