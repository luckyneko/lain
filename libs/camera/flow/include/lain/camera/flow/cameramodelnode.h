#pragma once

#include <lain/camera/calibration/report.h>
#include <lain/camera/cameramodel.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>

namespace lain::camera
{
	// The camera model a calibration report holds. A report with none (a failed calibration) CLEARS
	// the output, so whatever uses the model is suppressed rather than run on nothing; the report
	// itself says why.
	//
	// It passes the model on whatever the verdict. Whether a Rejected or Exploratory model is fit for
	// what comes next is the report's to say and the graph's to decide, and a node that dropped it
	// would make that decision where nobody can see it.
	class CameraModelNode : public flow::Node
	{
	public:
		CameraModelNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<CameraModelNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_report; // "report" (calibration::Report)
		flow::PortId m_model;  // "model" (CameraModel)
	};
} // namespace lain::camera
