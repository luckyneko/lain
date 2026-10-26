#pragma once

#include <lain/camera/comparison.h>
#include <lain/camera/registration/report.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

namespace lain::camera
{
	// How far a registered rig is from an expected one (camera::compareRigs): a registration's rig
	// against a synthetic capture's truth or a survey, both bound from rig documents or one passed on by
	// registeredRig. The first node that consumes a registered rig.
	//
	// A comparison it cannot make (the found rig's reference is not in the expected one) FAILS the node
	// with the reason, rather than passing on a comparison that says nothing; the cameras only one rig
	// holds are named in the comparison, not refused.
	class CompareRigsNode : public flow::Node
	{
	public:
		CompareRigsNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<CompareRigsNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_expected;   // "expected" (registration::RegisteredRig)
		flow::PortId m_found;	   // "found" (registration::RegisteredRig)
		flow::PortId m_comparison; // "comparison" (RigComparison)
	};
} // namespace lain::camera
