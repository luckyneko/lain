#include "lain/flow/example/loadimagenode.h"

#include <lain/io/image/load.h>

#include <utility>

namespace lain::flow::example
{
	LoadImageNode::LoadImageNode(std::string uri)
		: Node("LoadImage")
		, m_uri(std::move(uri))
	{
		m_out = addOutput<image::Image>("image");
	}

	void LoadImageNode::compute()
	{
		// io::image::load reads the bytes and dispatches to the reader for the uri's
		// extension; nullopt (missing/unknown/bad) becomes an invalid Image on the port.
		auto loaded = lain::io::image::load(m_uri);
		output(m_out).set(loaded ? std::move(*loaded) : image::Image{});
	}
} // namespace lain::flow::example
