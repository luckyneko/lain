#include "dump.h"

#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/image/image.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>

namespace flowview
{
	using namespace lain::flow;

	// One pixel of an RGBA8 buffer as "rgba(r,g,b,a)". Byte order is [R, G, B, A].
	static std::string pixel(const std::uint8_t* px, std::size_t texel)
	{
		const std::size_t i = texel * 4;
		std::ostringstream s;
		s << "rgba(" << int(px[i]) << ',' << int(px[i + 1]) << ',' << int(px[i + 2]) << ',' << int(px[i + 3]) << ')';
		return s.str();
	}

	// A lain::image::Image port value: its extent (via toString) plus its top-left /
	// bottom-right pixels, read straight from the CPU buffer — no device.
	static std::string imageLabel(const lain::image::Image& img)
	{
		if (!img.valid())
			return img.toString() + " (invalid)";
		std::ostringstream s;
		s << img.toString();
		const std::size_t count = img.pixelCount();
		s << " TL=" << pixel(img.data(), 0) << " BR=" << pixel(img.data(), count - 1);
		return s.str();
	}

	// A port's current value rendered as text. A lain::image::Image shows its corner
	// pixels; everything else — CPU values, the empty slot, and unrenderable types — goes
	// through Port::describe() (the shared meta::toString pathway), so this adapter owns
	// only the image-specific branch.
	static std::string valueLabel(const Port& port)
	{
		if (port.ready() && port.type() == typeid(lain::image::Image))
			return imageLabel(port.value().get<lain::image::Image>());
		return port.describe();
	}

	static void dumpPort(std::ostream& out, const char* tag, const Port& port)
	{
		out << tag << port.name() << ": " << valueLabel(port) << '\n';
	}

	void dumpGraph(std::ostream& out, const Graph& graph)
	{
		out << "graph: " << graph.nodeCount() << " node(s)\n";
		for (const NodeId id : graph.topoOrder())
		{
			const Node& node = graph.node(id);
			out << '[' << id.value() << "] " << node.name() << '\n';
			for (PortIndex i = 0; i < node.inputCount(); ++i)
				dumpPort(out, "  in  ", node.input(i));
			for (PortIndex i = 0; i < node.outputCount(); ++i)
				dumpPort(out, "  out ", node.output(i));
		}
	}
} // namespace flowview
