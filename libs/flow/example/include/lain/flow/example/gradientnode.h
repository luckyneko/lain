#pragma once

#include <archimedes/acmDevice.h>
#include <archimedes/acmTexture.h>
#include <archimedes/acmTypes.h>
#include <lain/flow/node.h>

namespace lain::flow::example
{
	// A GPU source node: fills an owned acm::Texture with a procedural RGBA
	// gradient and emits it on its output port. The simplest node on the GPU-port
	// path — it proves a graph can carry an acm::Texture end to end (for the viewer
	// to preview zero-copy) and doubles as flowview's smoke scene.
	//
	// Pixels are CPU-generated and uploaded here only because the environment has
	// no shader compiler; a compute-shader variant has the same shape (hold a
	// Device, write a Texture) and can drop in once SPIR-V is available.
	class GradientNode : public Node
	{
	public:
		GradientNode(acm::Device device, acm::Extent2D extent);

		// Index of the acm::Texture output port.
		PortIndex texturePort() const { return m_out; }

		void compute() override;

	private:
		acm::Device m_device;
		acm::Extent2D m_extent;
		acm::Texture m_texture; // persistent owned output, re-uploaded each compute
		PortIndex m_out;
	};
} // namespace lain::flow::example
