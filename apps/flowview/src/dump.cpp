#include "dump.h"

#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/image/image.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <sstream>
#include <string>

namespace flowview
{
	using namespace lain::flow;

	// One texel of any PixelFormat as "(c0,c1,...)", read per the format's descriptor —
	// U8 as an int, U16 as its value, F32 as a float — so a loaded RGB8/Gray16/etc. image
	// dumps correctly, not just the RGBA8 the gradient scene produces.
	static std::string pixel(const lain::image::Image& img, std::size_t texel)
	{
		const auto desc = img.descriptor();
		const std::uint8_t* base = img.data() + texel * desc.bytesPerPixel();
		std::ostringstream s;
		s << '(';
		for (int c = 0; c < desc.channelCount(); ++c)
		{
			if (c != 0)
				s << ',';
			const std::uint8_t* p = base + static_cast<std::size_t>(c) * desc.bytesPerChannel();
			switch (desc.channelType)
			{
				case lain::image::ChannelType::U8:
					s << int(*p);
					break;
				case lain::image::ChannelType::U16:
				{
					std::uint16_t v;
					std::memcpy(&v, p, sizeof(v));
					s << v;
					break;
				}
				case lain::image::ChannelType::F32:
				{
					float v;
					std::memcpy(&v, p, sizeof(v));
					s << v;
					break;
				}
			}
		}
		s << ')';
		return s.str();
	}

	// A lain::image::Image port value: its extent + format (via toString) plus its top-left
	// / bottom-right pixels, read straight from the CPU buffer — no device.
	static std::string imageLabel(const lain::image::Image& img)
	{
		if (!img.valid())
			return img.toString() + " (invalid)";
		std::ostringstream s;
		s << img.toString();
		const std::size_t count = img.pixelCount();
		s << " TL=" << pixel(img, 0) << " BR=" << pixel(img, count - 1);
		return s.str();
	}

	// A port's current value rendered as text. A lain::image::Image shows its corner pixels;
	// everything else — CPU values, the empty slot, and unrenderable types — goes through
	// Evaluation::describe (the shared meta::toString pathway), so this adapter owns only the
	// image-specific branch.
	//
	// The branch asks what the VALUE holds, never what the port declares. Those are the same
	// question only until a port can be RETYPED (ADR-0022): after one, a slot still carries what the
	// last run left there, so a port declaring Image over a leftover int would have taken this
	// branch and thrown std::bad_any_cast out of `flowview run`. describe() handles the mismatch.
	static std::string valueLabel(const Evaluation& evaluation, PortAddress address, const Port&)
	{
		const PortValue& value = evaluation.value(address);
		if (value.holds<lain::image::Image>())
			return imageLabel(value.get<lain::image::Image>());
		return evaluation.describe(address);
	}

	static void dumpPort(std::ostream& out, const char* tag, const Evaluation& evaluation, NodeId node, const Port& port)
	{
		out << tag << port.name() << ": " << valueLabel(evaluation, PortAddress{node, port.id()}, port) << '\n';
	}

	void dumpGraph(std::ostream& out, const Graph& graph, const Evaluation& evaluation)
	{
		out << "graph: " << graph.nodeCount() << " node(s)\n";
		for (const NodeId id : graph.topoOrder())
		{
			const Node& node = graph.node(id);
			// A truncated id keeps the dump scannable; the full uuid is in the document.
			out << '[' << id.shortString() << "] " << node.name() << '\n';
			for (std::size_t i = 0; i < node.inputCount(); ++i)
				dumpPort(out, "  in  ", evaluation, id, node.input(i));
			for (std::size_t i = 0; i < node.outputCount(); ++i)
				dumpPort(out, "  out ", evaluation, id, node.output(i));
		}
	}
} // namespace flowview
