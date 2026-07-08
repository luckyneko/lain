#include "lain/flow/example/loadimagenode.h"

#include <lain/io/image/load.h>

#include <filesystem>
#include <string>
#include <utility>

namespace lain::flow::example
{
	LoadImageNode::LoadImageNode(std::string uri)
		: Node("LoadImage")
	{
		// A std::filesystem::path param (not a plain string) so the adapter renders a file
		// picker, not a text field — the type carries the widget intent (ADR-0005).
		m_path = addParam<std::filesystem::path>("path", std::filesystem::path(std::move(uri)));
		m_out = addOutput<image::Image>("image");
	}

	void LoadImageNode::compute()
	{
		// Read the path param the way a real node reads config; the gui edits this same slot.
		const std::string path = param(m_path).get<std::filesystem::path>().string();

		// io::image::load reads the bytes and dispatches to the reader for the uri's
		// extension; nullopt (missing/unknown/bad) becomes an invalid Image on the port.
		auto loaded = lain::io::image::load(path);
		output(m_out).set(loaded ? std::move(*loaded) : image::Image{});
	}
} // namespace lain::flow::example
