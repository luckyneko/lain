#include "lain/flow/example/opensequencenode.h"

#include <lain/io/sequence/open.h>

#include <filesystem>
#include <string>
#include <utility>

namespace lain::flow::example
{
	OpenSequenceNode::OpenSequenceNode(std::string uri)
		: Node("OpenSequence")
	{
		// A std::filesystem::path so the adapter renders a picker rather than a text field — the
		// type carries the widget intent (ADR-0005). It names a folder or a ####-numbered pattern
		// today, and a video file once slice 5 lands, which is exactly why the pin is a path and
		// the node is not called OpenVideo.
		m_path = addInput<std::filesystem::path>("path", Default{std::filesystem::path(std::move(uri))});
		m_out = addOutput<lain::media::FrameSequence>("sequence");
	}

	void OpenSequenceNode::compute(NodeEvaluation& evaluation) const
	{
		const std::filesystem::path path = evaluation.input(m_path).get<std::filesystem::path>();

		std::optional<lain::media::FrameSequence> sequence = lain::io::sequence::open(path.string());
		if (!sequence.has_value())
		{
			// open() logged why. Clearing rather than emitting an empty sequence keeps the two
			// distinguishable: an empty sequence is a folder that exists and holds nothing, and
			// downstream may legitimately act on it; no value at all is what a missing folder means,
			// and it suppresses the rest of the graph.
			evaluation.output(m_out).clear();
			return;
		}
		evaluation.output(m_out).set(std::move(*sequence));
	}
} // namespace lain::flow::example
