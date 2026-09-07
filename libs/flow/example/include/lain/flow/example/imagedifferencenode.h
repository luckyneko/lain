#pragma once

#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>

namespace lain::flow::example
{
	// Two images in, ONE number out: their mean absolute difference, normalised to [0, 1]. Half of
	// what makes a WHILE loop's condition real (M11 / ADR-0021) — "has this stopped changing?" is a
	// measurement the graph makes, and CompareNode turns it into the bool the reserved `continue`
	// pin reads.
	//
	// It is an ORDINARY node: a loop's condition path needs no engine support beyond the pin itself.
	//
	// A MEASUREMENT, not a blend — so ADR-0003's op-class enforcement does not apply and nothing is
	// converted to Linear here. It compares the bytes it is given, which is what makes "the last
	// iteration produced the same picture" mean what a caller expects; a colour-managed difference
	// would answer a different question, and is not one anything asks.
	//
	// Images of differing size or pixel format are REJECTED rather than resized or converted, as
	// CombineNode rejects a ragged collection: reconciling them would make the number mean something
	// the caller did not ask for. A refusal produces no value, so ADR-0007 suppresses downstream —
	// and inside a loop that is an iteration that FAILED, which is exactly right.
	class ImageDifferenceNode : public Node
	{
	public:
		ImageDifferenceNode();

		void compute(NodeEvaluation& evaluation) const override;

	private:
		PortId m_a;			 // image::Image
		PortId m_b;			 // image::Image
		PortId m_difference; // float, in [0, 1]
	};
} // namespace lain::flow::example
