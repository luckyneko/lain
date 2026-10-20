#include "lain/camera/flow/calibratecameranode.h"

#include "detection.h"

#include <lain/camera/calibration/board.h>
#include <lain/camera/calibration/request.h>

#include <algorithm>
#include <cstdint>

namespace lain::camera
{
	CalibrateCameraNode::CalibrateCameraNode()
		: CalibrationNode("CalibrateCamera")
	{
		m_board = addInput<board::Specification>("board");
		declareRequest();

		// What only board calibration takes, with the request's and the search's own defaults, so a
		// fresh node asks for exactly what calibration::Request and board::DetectionRequest do.
		const calibration::Request defaults;
		const board::DetectionRequest search;
		m_maximumViews = addParam<int>("maximumViews", int(defaults.maximumViews));
		m_longestSide = addParam<int>("longestSide", 0);
		m_refineNative = addParam<bool>("refineNative", search.refineAtNativeResolution);
		m_minimumCorners = addParam<int>("minimumCorners", int(search.minimumCorners));

		m_report = addOutput<calibration::Report>("report");
	}

	void CalibrateCameraNode::compute(flow::NodeEvaluation& evaluation) const
	{
		calibration::Request request = this->request(evaluation);
		request.maximumViews = std::uint32_t(std::max(0, param(m_maximumViews).get<int>()));
		const board::DetectionRequest search = detail::detectionRequest(
			param(m_longestSide).get<int>(), param(m_refineNative).get<bool>(), param(m_minimumCorners).get<int>());

		evaluation.output(m_report).set(calibration::board::calibrate(
			footage(evaluation), evaluation.input(m_board).get<board::Specification>(), search, request));
	}
} // namespace lain::camera
