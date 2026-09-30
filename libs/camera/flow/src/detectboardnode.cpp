#include "lain/camera/flow/detectboardnode.h"

#include "detection.h"

namespace lain::camera
{
	DetectBoardNode::DetectBoardNode()
		: Node("DetectBoard")
	{
		// The data inputs lead and the settings follow, as FrameAt's do: a port's position is how
		// tests and hand-built graphs address it.
		m_image = addInput<image::Image>("image");
		m_board = addInput<board::Specification>("board");
		// Optional: provenance, not data. A frame that did not arrive leaves the observation naming
		// none rather than suppressing the detection.
		m_frame = addInput<media::FrameRef>("frame", flow::Presence::Optional);

		m_longestSide = addParam<int>("longestSide", 0);
		m_refineNative = addParam<bool>("refineNative", true);
		m_minimumCorners = addParam<int>("minimumCorners", 4);

		m_report = addOutput<board::DetectionReport>("report");
	}

	void DetectBoardNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const flow::PortValue& frame = evaluation.input(m_frame);
		const board::DetectionRequest request = detail::detectionRequest(
			param(m_longestSide).get<int>(), param(m_refineNative).get<bool>(), param(m_minimumCorners).get<int>());
		evaluation.output(m_report).set(board::detect(evaluation.input(m_image).get<image::Image>(),
													  frame.empty() ? media::FrameRef{} : frame.get<media::FrameRef>(),
													  evaluation.input(m_board).get<board::Specification>(), request));
	}
} // namespace lain::camera
