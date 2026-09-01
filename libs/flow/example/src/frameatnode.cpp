#include "lain/flow/example/frameatnode.h"

#include <utility>

namespace lain::flow::example
{
	FrameAtNode::FrameAtNode()
		: Node("FrameAt")
	{
		// The DATA input leads and the setting follows it: a port's position is how tests and
		// hand-built graphs address it, so declaring `position` first would silently retarget every
		// connect(src, 0, frameAt, 0) onto the setting. Documents are unaffected — edges serialize
		// by port name — which is exactly why the hazard is easy to miss.
		m_sequence = addInput<lain::media::FrameSequence>("sequence");
		m_position = addInput<lain::media::FramePosition>("position", Default{lain::media::FramePosition{0}});

		m_image = addOutput<image::Image>("image");
		m_frame = addOutput<lain::media::FrameRef>("frame");
	}

	void FrameAtNode::compute(NodeEvaluation& evaluation) const
	{
		const PortValue& slot = evaluation.input(m_sequence);
		if (slot.empty())
		{
			// Nothing upstream — clear both, so absence propagates rather than this node serving
			// last run's frame.
			evaluation.output(m_image).clear();
			evaluation.output(m_frame).clear();
			return;
		}

		const lain::media::FrameSequence& sequence = slot.get<lain::media::FrameSequence>();
		const std::size_t position = evaluation.input(m_position).get<lain::media::FramePosition>().value;

		// Blocking, and an invalid Image on any failure — out of range, a decode error, or a frame
		// that does not match the sequence's declared spec. compute() is synchronous everywhere in
		// this engine, so a decode that takes time simply takes time.
		evaluation.output(m_image).set(sequence.image(position));

		// The identity travels even when the pixels did not arrive: an issue that has to report
		// WHICH frame failed needs this pin to be populated precisely then.
		evaluation.output(m_frame).set(sequence.frame(position));
	}
} // namespace lain::flow::example
