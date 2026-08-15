#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>

#include <filesystem>
#include <string>

namespace lain::flow::example
{
	// A CPU source node: loads an image file from a std::filesystem::path param (via
	// lain::io::image::load) and emits the decoded lain::image::Image on its output port. The
	// first node to bring a *real* file into a graph — flowview's cli-mode loads one and dumps
	// the result, and its path is editable in the gui (a path param renders as a file picker,
	// distinct from a plain string's text field; see ADR-0005).
	//
	// The ctor uri seeds the param's default (so `flowview --image` and the cli still work);
	// a palette-added node starts with an empty path, set in the gui. The reader registry must
	// be populated (io::image::registerImageCodecs, or a single codec's registerCodec) before
	// compute() runs — that is the app's job, not the node's. A load failure (missing file,
	// unknown format, decode error) leaves an invalid Image on the port; load logs the reason.
	// `path` is an INPUT WITH A DEFAULT (Node::addInput), which is how a map drives this node:
	// configuration alone would not do, because every element of a map shares one definition, so a
	// per-element path has to arrive as a value. Unconnected, it loads the file it is configured
	// with, so an existing scene behaves exactly as before.
	class LoadImageNode : public Node
	{
	public:
		explicit LoadImageNode(std::string uri = {});

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_pathIn; // optional std::filesystem::path input — overrides the param when present
		PortId m_path;	 // "path" param (std::filesystem::path)
		PortId m_out;
	};
} // namespace lain::flow::example
