#include "lain/camera/flow/cameramodelnode.h"

namespace lain::camera
{
	CameraModelNode::CameraModelNode()
		: Node("CameraModel")
	{
		m_report = addInput<calibration::Report>("report");
		m_model = addOutput<CameraModel>("model");
	}

	void CameraModelNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const calibration::Report& report = evaluation.input(m_report).get<calibration::Report>();
		if (report.model)
			evaluation.output(m_model).set(*report.model);
		else
			evaluation.output(m_model).clear();
	}
} // namespace lain::camera
