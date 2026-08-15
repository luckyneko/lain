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
		// A std::filesystem::path (not a plain string) so the adapter renders a file picker, not a
		// text field — the type carries the widget intent (ADR-0005). An input WITH A DEFAULT, so an
		// unconnected node still loads the file it is configured with, while a MAP can hand it a
		// different file per element.
		m_path = addInput<std::filesystem::path>("path", Default{std::filesystem::path(std::move(uri))});
		m_out = addOutput<image::Image>("image");
	}

	void LoadImageNode::compute(NodeEvaluation& evaluation) const
	{
		// An ordinary input read: unconnected, the slot carries the configured default.
		const std::string path = evaluation.input(m_path).get<std::filesystem::path>().string();

		// io::image::load reads the bytes and dispatches to the reader for the uri's
		// extension; nullopt (missing/unknown/bad) becomes an invalid Image on the port.
		auto loaded = lain::io::image::load(path);
		evaluation.output(m_out).set(loaded ? std::move(*loaded) : image::Image{});
	}
} // namespace lain::flow::example
