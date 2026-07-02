#include <lain/flow/example/tintnode.h>

#include <archimedes/archimedes.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lain::flow::example
{
	TintNode::TintNode(acm::Device device, float tintR, float tintG, float tintB)
		: Node("Tint")
		, m_device(device)
		, m_tintR(tintR)
		, m_tintG(tintG)
		, m_tintB(tintB)
	{
		m_in = addInput<acm::Texture>("texture");
		m_out = addOutput<acm::Texture>("texture");
	}

	void TintNode::compute()
	{
		// No upstream texture yet -> nothing to emit (leave the output slot as it was).
		if (!input(m_in).ready())
			return;

		const acm::Texture& src = input(m_in).get<acm::Texture>();
		if (!src.valid())
			return;

		const acm::Extent2D extent = src.getExtent();
		const std::size_t count = static_cast<std::size_t>(extent.width) * extent.height;
		if (count == 0)
			return;

		// Read the input back to the CPU, transitioning it to TransferSrc for the copy
		// and back to ShaderReadOnly so it stays samplable (the inspector previews it).
		acm::Buffer readback = m_device.createBuffer(count * 4, acm::BufferUsage::TransferDst);
		if (!readback.valid())
			return;
		m_device.submitSync([&](acm::CommandBuffer cmd)
							 {
			cmd.transitionImage(src, acm::ImageLayout::ShaderReadOnly, acm::ImageLayout::TransferSrc);
			cmd.copyTextureToBuffer(src, readback);
			cmd.transitionImage(src, acm::ImageLayout::TransferSrc, acm::ImageLayout::ShaderReadOnly); });

		const auto* in = static_cast<const std::uint8_t*>(readback.map());
		if (in == nullptr)
			return;

		// out = clamp(in * factor), alpha preserved. R8G8B8A8_Unorm order is [R, G, B, A].
		const auto tint = [](std::uint8_t v, float f) -> std::uint8_t
		{
			const float scaled = static_cast<float>(v) * f;
			return static_cast<std::uint8_t>(scaled > 255.0f ? 255.0f : scaled);
		};
		std::vector<std::uint8_t> pixels(count * 4);
		for (std::size_t i = 0; i < count; ++i)
		{
			pixels[i * 4 + 0] = tint(in[i * 4 + 0], m_tintR);
			pixels[i * 4 + 1] = tint(in[i * 4 + 1], m_tintG);
			pixels[i * 4 + 2] = tint(in[i * 4 + 2], m_tintB);
			pixels[i * 4 + 3] = in[i * 4 + 3];
		}

		// (Re)create the owned output to match the input extent, then upload.
		if (!m_texture.valid() || m_texture.getExtent().width != extent.width || m_texture.getExtent().height != extent.height)
			m_texture = m_device.createTexture(acm::Format::R8G8B8A8_Unorm, extent);

		m_texture.upload(pixels.data(), pixels.size());
		output(m_out).set(m_texture);
	}
} // namespace lain::flow::example
