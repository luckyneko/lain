#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/colorspace.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>

namespace lain::flow::example
{
	// Declare and convert an image's pixel format and colour space — the node that lets a graph
	// hand a video encoder something it will accept.
	//
	// It exists because the video writer REFUSES rather than degrades (ADR-0018): alpha, Linear and
	// Unspecified are not things a container can state, so a graph whose result carries any of them
	// cannot be rendered to video. Refusing without giving a caller any way to comply would just be
	// a wall, so this is the way to comply — the same move that added a float Constant when Blur's
	// sigma became a driveable input.
	//
	// TWO DISTINCT JOBS, kept as two params rather than one, because conflating them is how a
	// pipeline acquires a silent lie:
	//
	//   `assume` DECLARES what an UNTAGGED image already is. Nothing is converted; the tag is
	//   simply attached, because there is no source space to convert from. It applies only when the
	//   input is Unspecified — a tagged image is never retagged, since overriding a stated fact is
	//   not something a target-shaped param should be able to do by accident. (Untagged images are
	//   ordinary: lain's own PNG writer records no colour chunk, so a still saved and reloaded
	//   comes back Unspecified.)
	//
	//   `colorSpace` is the TARGET, reached by image::convert — a real transfer-curve conversion.
	//
	// `pixelFormat` is applied first, matching the order TintNode already normalises in.
	//
	// Alpha is deliberately absent. image::convert's alpha arm needs a stated source mode and has
	// the same untagged problem a third time, and no video codec carries alpha anyway — so the one
	// caller this node exists for would never use it. Add it when something needs it.
	class ConvertNode : public Node
	{
	public:
		ConvertNode();

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_assume;	 // what an untagged input is declared to be (no conversion)
		PortId m_format;	 // target PixelFormat
		PortId m_colorSpace; // target ColorSpace, reached by conversion
		PortId m_in;
		PortId m_out;
	};
} // namespace lain::flow::example
