#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>

#include <vector>

namespace lain::flow::example
{
	// The other end of a map: N images in, ONE image out — the reduce a map's gathered collection
	// feeds (M8 / ADR-0014). Averages them pixelwise, which is the simplest fold that visibly
	// depends on every element, so a wrong or missing element shows up in the result rather than
	// hiding behind a plausible-looking picture.
	//
	// It is an ORDINARY node: it takes one vector-valued input and needs no engine support at all.
	// That is the point — a map produces a normal collection value, and what consumes it is just a
	// node that declares a collection port. (CONTEXT.md, "Port arity": a vector-valued port needs no
	// engine work to carry; only looking INSIDE one from the engine did.)
	//
	// Images of differing size or format are REJECTED rather than resized or converted: silently
	// reconciling them would make the average mean something the caller did not ask for, and the
	// image library's own rule is to reject rather than degrade (ADR-0003).
	class CombineNode : public Node
	{
	public:
		CombineNode();

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_in;  // std::vector<image::Image>
		PortId m_out; // image::Image
	};
} // namespace lain::flow::example
