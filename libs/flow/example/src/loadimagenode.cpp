#include "lain/flow/example/loadimagenode.h"

#include <lain/flow/paramtypes.h> // FilePath
#include <lain/io/image/load.h>

#include <string>
#include <utility>

namespace lain::flow::example
{
	LoadImageNode::LoadImageNode(std::string uri)
		: Node("LoadImage")
	{
		m_path = addParam<FilePath>("path", FilePath(std::move(uri)));
		m_out = addOutput<image::Image>("image");
	}

	void LoadImageNode::compute()
	{
		// Read the path param the way a real node reads config; the gui edits this same slot.
		const std::string& path = param(m_path).get<FilePath>().string();

		// io::image::load reads the bytes and dispatches to the reader for the uri's
		// extension; nullopt (missing/unknown/bad) becomes an invalid Image on the port.
		auto loaded = lain::io::image::load(path);
		output(m_out).set(loaded ? std::move(*loaded) : image::Image{});
	}
} // namespace lain::flow::example
