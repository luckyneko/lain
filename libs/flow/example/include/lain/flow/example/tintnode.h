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
		// tintR/G/B are per-channel multipliers in [0, 1] (e.g. 1.0, 0.5, 0.5 keeps red,
		// halves green and blue) — they seed a single editable "tint" Color param, which
		// the gui renders as a colour swatch.
		TintNode(float tintR, float tintG, float tintB);

		// Index of the lain::image::Image input / output ports.
		PortIndex inputPort() const { return m_in; }
		PortIndex imagePort() const { return m_out; }

		void compute() override;

	private:
		PortIndex m_tint; // "tint" param (image::ColorRGBf) — per-channel RGB multiplier
		PortIndex m_in;
		PortIndex m_out;
	};
} // namespace lain::flow::example
