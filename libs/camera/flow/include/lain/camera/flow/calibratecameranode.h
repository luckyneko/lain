#pragma once

#include "lain/camera/flow/calibrationnode.h"

#include <lain/camera/board/specification.h>
#include <lain/camera/calibration/report.h>
#include <lain/flow/evaluation.h>

namespace lain::camera
{
	// Board calibration over footage (calibration::board::calibrate): detects the board in every
	// frame, selects and holds out views, estimates the chosen distortion `model`, validates and
	// resamples, and gives a verdict. Always a `report`, successful or failed.
	//
	// ONE node for the whole method, not detect -> select -> estimate -> validate as a chain: what
	// makes the result mean something is the method's decisions taken together (which views, which
	// held out, what the verdict is held to), and a graph that rewired one of them would produce a
	// report that claims a method it did not follow.
	//
	// `imported` is an optional camera model taking part through `importedPolicy` (CalibrationNode):
	// ignored, used as the starting point, or held exactly and only validated. Detection settings mean
	// what they mean on detectBoard. Frames and resamples run on the process pool unless
	// `deterministic` asks for the same work serially, in canonical order.
	class CalibrateCameraNode : public CalibrationNode
	{
	public:
		CalibrateCameraNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<CalibrateCameraNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_board;		   // "board" (board::Specification)
		flow::PortId m_maximumViews;   // "maximumViews" (int)
		flow::PortId m_longestSide;	   // "longestSide" (int, 0 = native)
		flow::PortId m_refineNative;   // "refineNative" (bool)
		flow::PortId m_minimumCorners; // "minimumCorners" (int)
		flow::PortId m_report;		   // "report" (calibration::Report)
	};
} // namespace lain::camera
