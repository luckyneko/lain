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
	//
	// Its size is `width` / `height`, inputs with defaults: the constructor's values are only
	// the defaults a fresh node starts from. A default is a param underneath, so the size is
	// saved with the document; before, it was a plain member, and a document rendered at
	// whatever size the process that loaded it happened to construct gradients at. A size
	// below 1 produces no value.
	class GradientNode : public Node
	{
	public:
		GradientNode(std::uint32_t width, std::uint32_t height);

		std::unique_ptr<Node> clone() const override { return std::make_unique<GradientNode>(*this); }
		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_width;	 // int
		PortId m_height; // int
		PortId m_out;
	};
} // namespace lain::flow::example
