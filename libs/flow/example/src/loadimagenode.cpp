#include "lain/flow/example/loadimagenode.h"

#include "lain/flow/example/setting.h"

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
		// Wired wins over configured — the one rule, from setting.h. Unconnected, the param stands,
		// which keeps every existing scene working.
		const std::string path = setting<std::filesystem::path>(*this, evaluation, m_pathIn, m_path).string();

		// io::image::load reads the bytes and dispatches to the reader for the uri's
		// extension; nullopt (missing/unknown/bad) becomes an invalid Image on the port.
		auto loaded = lain::io::image::load(path);
		evaluation.output(m_out).set(loaded ? std::move(*loaded) : image::Image{});
	}
} // namespace lain::flow::example
