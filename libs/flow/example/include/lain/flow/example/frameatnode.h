#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>
#include <lain/media/frameposition.h>
#include <lain/media/frameref.h>
#include <lain/media/framesequence.h>

namespace lain::flow::example
{
	// Decodes one frame of a sequence: the point at which footage becomes pixels a graph can
	// process. The host varies `position` once per iteration to drive a render (ADR-0018).
	//
	// TWO OUTPUTS, NOT AN AGGREGATE. `image` and `frame` are separate pins because a `Frame` struct
	// would fork the node type system — every existing node takes an Image, so a Frame would need
	// an unwrap at every junction — and because the next caller would want Image + depth, and the
	// one after Image + mask. Provenance travels beside the pixels for anything that needs it, and
	// costs nothing to ignore for everything that does not. The accepted price: provenance does not
	// survive processing automatically, so a consumer that needs the frame wires `frame` around the
	// nodes in between.
	//
	// `position` is an input with a default so a graph renders something the moment it is opened,
	// rather than sitting suppressed until a host binds a position.
	//
	// A position past the end of the sequence yields an INVALID image, not a cleared output: the
	// sequence answered, and what it said was "not that one". A missing sequence upstream clears
	// both outputs, so absence propagates.
	class FrameAtNode : public Node
	{
	public:
		FrameAtNode();

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_sequence; // "sequence" (media::FrameSequence)
		PortId m_position; // "position" input with a default (media::FramePosition)
		PortId m_image;	   // "image" (image::Image)
		PortId m_frame;	   // "frame" (media::FrameRef) — which frame of which source this is
	};
} // namespace lain::flow::example
