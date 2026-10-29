#include "lain/camera/flow/calibrationmethodnode.h"

namespace lain::camera
{
	CalibrationMethodNode::CalibrationMethodNode()
		: Node("CalibrationMethod")
	{
		m_method = addInput<calibration::Method>("method", flow::Default{calibration::Method::BoardThenTargetless});
		m_board = addOutput<bool>("board");
		m_targetless = addOutput<bool>("targetless");
	}

	void CalibrationMethodNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const calibration::Method method = evaluation.input(m_method).get<calibration::Method>();
		evaluation.output(m_board).set(calibration::includes(method, calibration::Method::Board));
		evaluation.output(m_targetless).set(calibration::includes(method, calibration::Method::Targetless));
	}
} // namespace lain::camera
