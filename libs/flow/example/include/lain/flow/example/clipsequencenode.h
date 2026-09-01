#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/media/framesequence.h>

#include <cstddef>

namespace lain::flow::example
{
	// Takes a run of frames out of a sequence — the first of ADR-0018's list operations to reach
	// the graph.
	//
	// SELECTION, NOT EDIT: a clip re-bases position and preserves identity, so frame 0 of
	// clip(seq, 100, 50) is position 0 and still ordinal 100 of its source. That is what keeps a
	// sequence-valued output honest — it can express clip, reorder, concatenate and subset, and
	// structurally cannot carry processed pixels, because a frame a graph computed has no source
	// to reference.
	//
	// `count` is CLAMPED to what exists rather than refused: asking for more than remains is an
	// ordinary thing to do at the end of a timeline, and the honest answer is what there was.
	class ClipSequenceNode : public Node
	{
	public:
		ClipSequenceNode(std::size_t position = 0, std::size_t count = 0);

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_sequence; // "sequence" (media::FrameSequence)
		PortId m_position; // "position" input with a default — where the clip starts
		PortId m_count;	   // "count" input with a default — how many frames, clamped
		PortId m_out;	   // "clipped" (media::FrameSequence)
	};
} // namespace lain::flow::example
