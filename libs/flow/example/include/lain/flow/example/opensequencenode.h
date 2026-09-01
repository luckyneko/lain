#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/media/framesequence.h>

#include <filesystem>
#include <string>

namespace lain::flow::example
{
	// Opens a uri as a media::FrameSequence — the source node that brings footage into a graph.
	//
	// ONE node kind for every medium, not one per medium (ADR-0019, amended). It goes through
	// io::sequence::open, so a document does not change vocabulary when its footage goes from a
	// folder of stills to an mp4, and a build without video support reports that it cannot open the
	// file rather than dropping the node and its edges as an unknown kind.
	//
	// `path` is an INPUT WITH A DEFAULT for the same reason LoadImageNode's is: configuration alone
	// would not do, because every element of a map shares one definition, so a per-element source
	// has to be able to arrive as a value. Unconnected, it opens what it is configured with.
	//
	// A uri that cannot be opened CLEARS the output rather than emitting an empty sequence — the
	// two mean different things here. An empty sequence is a value (a folder that exists and holds
	// no images); nothing at all is the absence that suppresses everything downstream (ADR-0007),
	// which is what a missing folder should do.
	class OpenSequenceNode : public Node
	{
	public:
		explicit OpenSequenceNode(std::string uri = {});

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_path; // "path" input with a default (std::filesystem::path)
		PortId m_out;  // "sequence" (media::FrameSequence)
	};
} // namespace lain::flow::example
