#include "dump.h"

#include <archimedes/archimedes.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace flowview
{
	using namespace lain::flow;

	// One pixel of a readback buffer as "rgba(r,g,b,a)". Memory order for
	// R8G8B8A8_Unorm is [R, G, B, A].
	static std::string pixel(const std::uint8_t* px, std::size_t texel)
	{
		const std::uint8_t* p = px + texel * 4;
		std::ostringstream s;
		s << "rgba(" << int(p[0]) << ',' << int(p[1]) << ',' << int(p[2]) << ',' << int(p[3]) << ')';
		return s.str();
	}

	// An acm::Texture port value: its type name (from the port), extent, plus its
	// top-left / bottom-right pixels read back through `device` (skipped when there's
	// no device or the readback can't be set up — the dump's structure is unchanged
	// either way).
	static std::string textureLabel(std::string_view typeName, const acm::Texture& texture, acm::Device device)
	{
		if (!texture.valid())
			return std::string(typeName) + " (invalid)";

		const acm::Extent2D extent = texture.getExtent();
		std::ostringstream s;
		s << typeName << ' ' << extent.width << 'x' << extent.height;

		if (!device.valid() || extent.width == 0 || extent.height == 0)
			return s.str();

		const std::size_t count = std::size_t(extent.width) * extent.height;
		acm::Buffer readback = device.createBuffer(count * 4, acm::BufferUsage::TransferDst);
		if (!readback.valid())
			return s.str() + " (readback failed)";

		device.submitSync([&](acm::CommandBuffer cmd)
						  {
			cmd.transitionImage(texture, acm::ImageLayout::ShaderReadOnly, acm::ImageLayout::TransferSrc);
			cmd.copyTextureToBuffer(texture, readback); });

		const auto* px = static_cast<const std::uint8_t*>(readback.map());
		if (px == nullptr)
			return s.str();

		s << " TL=" << pixel(px, 0) << " BR=" << pixel(px, count - 1);
		return s.str();
	}

	// A port's current value rendered as text. An acm::Texture reads back its corner
	// pixels (cli-only, device-dependent); everything else — CPU values, the empty slot,
	// and unrenderable types — goes through Port::describe() (the shared meta::toString
	// pathway), so this adapter owns only the texture-specific branch.
	static std::string valueLabel(const Port& port, acm::Device device)
	{
		if (port.ready() && port.type() == typeid(acm::Texture))
			return textureLabel(port.typeName(), port.value().get<acm::Texture>(), device);
		return port.describe();
	}

	static void dumpPort(std::ostream& out, const char* tag, const Port& port, acm::Device device)
	{
		out << tag << port.name() << ": " << valueLabel(port, device) << '\n';
	}

	void dumpGraph(std::ostream& out, const Graph& graph, acm::Device device)
	{
		out << "graph: " << graph.nodeCount() << " node(s)\n";
		for (const NodeId id : graph.topoOrder())
		{
			const Node& node = graph.node(id);
			out << '[' << id.value() << "] " << node.name() << '\n';
			for (PortIndex i = 0; i < node.inputCount(); ++i)
				dumpPort(out, "  in  ", node.input(i), device);
			for (PortIndex i = 0; i < node.outputCount(); ++i)
				dumpPort(out, "  out ", node.output(i), device);
		}
	}
} // namespace flowview
