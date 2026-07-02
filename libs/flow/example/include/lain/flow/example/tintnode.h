#pragma once

#include <archimedes/acmDevice.h>
#include <archimedes/acmTexture.h>
#include <archimedes/acmTypes.h>
#include <lain/flow/node.h>

namespace lain::flow::example
{
	// A GPU transform node: reads an acm::Texture on its input, multiplies each pixel's
	// RGB by a fixed tint (alpha preserved), and emits the result on its output. The
	// simplest node that both consumes and produces a texture, so a graph edge actually
	// carries data through it — the connectable counterpart to GradientNode.
	//
	// Like GradientNode the pixel work is CPU-side (read the input back, tint,
	// re-upload) because the environment has no shader compiler; the shape is the same
	// as a compute-shader variant would take.
	class TintNode : public Node
	{
	public:
		// tintR/G/B are per-channel multipliers (e.g. 1.0, 0.5, 0.5 keeps red, halves
		// green and blue). Values above 1 are clamped at 255 per channel.
		TintNode(acm::Device device, float tintR, float tintG, float tintB);

		// Index of the acm::Texture input / output ports.
		PortIndex inputPort() const { return m_in; }
		PortIndex texturePort() const { return m_out; }

		void compute() override;

	private:
		acm::Device m_device;
		float m_tintR;
		float m_tintG;
		float m_tintB;
		acm::Texture m_texture; // owned output, (re)created to match the input extent
		PortIndex m_in;
		PortIndex m_out;
	};
} // namespace lain::flow::example
