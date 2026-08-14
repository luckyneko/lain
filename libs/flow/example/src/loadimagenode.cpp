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
		// Optional, so an unconnected node still loads from its param and the smoke scene is
		// unchanged; connected, it is what lets a MAP hand this node a different file per element.
		m_pathIn = addInput<std::filesystem::path>("path", Presence::Optional);
		m_out = addOutput<image::Image>("image");
	}

	void LoadImageNode::compute(NodeEvaluation& evaluation) const
	{
		// A connected path wins over the param: the param is this node's own configuration, while a
		// wired value is what the graph is asking for right now (a map's element, say). Unconnected,
		// the input is empty and the param stands — which keeps every existing scene working.
		const PortValue& wired = evaluation.input(m_pathIn);
		const std::string path = wired.holds<std::filesystem::path>()
									 ? wired.get<std::filesystem::path>().string()
									 : param(m_path).get<std::filesystem::path>().string();

		// io::image::load reads the bytes and dispatches to the reader for the uri's
		// extension; nullopt (missing/unknown/bad) becomes an invalid Image on the port.
		auto loaded = lain::io::image::load(path);
		evaluation.output(m_out).set(loaded ? std::move(*loaded) : image::Image{});
	}
} // namespace lain::flow::example
