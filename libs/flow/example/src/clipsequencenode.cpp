#include "lain/flow/example/clipsequencenode.h"

#include <lain/media/frameposition.h>
#include <lain/media/operations.h>

#include <utility>

namespace lain::flow::example
{
	ClipSequenceNode::ClipSequenceNode(std::size_t position, std::size_t count)
		: Node("ClipSequence")
	{
		// Data input first, settings after — see FrameAtNode for why the order is load-bearing.
		m_sequence = addInput<lain::media::FrameSequence>("sequence");

		// A FramePosition for the start, because that is what it is: a position in a frame
		// sequence, and typing it as a plain count would hide that from an editor that wants to
		// offer a timeline. `count` really is a count, so it stays an int.
		m_position = addInput<lain::media::FramePosition>("position", Default{lain::media::FramePosition{position}});
		m_count = addInput<int>("count", Default{static_cast<int>(count)});

		m_out = addOutput<lain::media::FrameSequence>("clipped");
	}

	void ClipSequenceNode::compute(NodeEvaluation& evaluation) const
	{
		const PortValue& slot = evaluation.input(m_sequence);
		if (slot.empty())
		{
			evaluation.output(m_out).clear();
			return;
		}

		const lain::media::FrameSequence& sequence = slot.get<lain::media::FrameSequence>();
		const std::size_t position = evaluation.input(m_position).get<lain::media::FramePosition>().value;
		const int count = evaluation.input(m_count).get<int>();

		// A negative count is a caller error with an obvious reading — take nothing — rather than a
		// wildly large unsigned one, which is what the cast alone would produce.
		const std::size_t taken = count > 0 ? static_cast<std::size_t>(count) : 0;
		evaluation.output(m_out).set(lain::media::clip(sequence, position, taken));
	}
} // namespace lain::flow::example
