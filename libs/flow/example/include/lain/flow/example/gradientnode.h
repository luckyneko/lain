#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>

#include <cstdint>

namespace lain::flow::example
{
	// A CPU source node: fills a lain::image::Image with a procedural RGBA gradient and
	// emits it on its output port. The simplest node — it proves a graph carries an image
	// payload end to end (the viewer previews it) and doubles as flowview's smoke scene.
	//
	// Pixels are CPU-generated: this node needs no GPU device. A future GPU/compute variant
	// (writing an acm::Texture through a ComputePipeline) would take an injected device
	// context at compute() time rather than owning a device.
	class GradientNode : public Node
	{
	public:
		GradientNode(std::uint32_t width, std::uint32_t height);

		void compute(NodeEvaluation& evaluation) const override;

	private:
		std::uint32_t m_width;
		std::uint32_t m_height;
		PortId m_out;
	};
} // namespace lain::flow::example
