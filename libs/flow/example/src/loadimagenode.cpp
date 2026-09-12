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
		const std::filesystem::path path = evaluation.input(m_path).get<std::filesystem::path>();

		// io::image::load reads the bytes and dispatches to the reader for the uri's
		// extension; nullopt (missing/unknown/bad) becomes an invalid Image on the port.
		// fromPath, not the path's text: a uri is what load() names a resource by, and the
		// conversion from a path is the one that has to be spelled out (ADR-0023).
		auto loaded = lain::io::image::load(lain::core::Uri::fromPath(path));
		evaluation.output(m_out).set(loaded ? std::move(*loaded) : image::Image{});
	}
} // namespace lain::flow::example
