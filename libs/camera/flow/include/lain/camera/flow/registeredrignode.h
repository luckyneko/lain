#pragma once

#include <lain/camera/registration/report.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

namespace lain::camera
{
	// The rig a registration report holds (registration::Report::rig): every camera's transform, the
	// reference and the scale. A report with none (a failed registration) CLEARS the output, so whatever
	// uses the rig is suppressed rather than run on nothing; the report itself says why.
	//
	// It passes the rig on whatever the verdict, as cameraModel passes a model: whether a Rejected or
	// Exploratory rig is fit for what comes next is the report's to say and the graph's to decide.
	class RegisteredRigNode : public flow::Node
	{
	public:
		RegisteredRigNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<RegisteredRigNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_report; // "report" (registration::Report)
		flow::PortId m_rig;	   // "rig" (registration::RegisteredRig)
	};
} // namespace lain::camera
