#pragma once

#include <lain/flow/node.h>
#include <lain/image/image.h>

#include <string>

namespace lain::flow::example
{
	// A CPU source node: loads an image file from a uri (via lain::io::image::load) and
	// emits the decoded lain::image::Image on its output port. The first node to bring a
	// *real* file into a graph — flowview's cli-mode loads one and dumps the result.
	//
	// The reader registry must be populated (io::image::registerImageCodecs, or a single
	// codec's registerCodec) before compute() runs — that is the app's job, not the node's.
	// A load failure (missing file, unknown format, decode error) leaves an invalid Image on
	// the port; io::image::load logs the reason. Pure CPU: no device needed.
	class LoadImageNode : public Node
	{
	public:
		explicit LoadImageNode(std::string uri);

		// Index of the lain::image::Image output port.
		PortIndex imagePort() const { return m_out; }

		void compute() override;

	private:
		std::string m_uri;
		PortIndex m_out;
	};
} // namespace lain::flow::example
