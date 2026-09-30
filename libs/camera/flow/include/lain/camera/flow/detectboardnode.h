#pragma once

#include <lain/camera/board/detection.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>
#include <lain/media/frameref.h>

namespace lain::camera
{
	// Looks for a board in one image: always a `report`, successful or failed (board::detect). Pairs
	// with frameAt, whose `frame` output names which frame the observation belongs to; unconnected,
	// the observation names no frame.
	//
	// Detection settings: `longestSide` searches a reduced image at most that many pixels on its
	// longer side (0 searches at native resolution), `refineNative` refines the corners found against
	// the source pixels, and `minimumCorners` is the fewest that count as an observation. None of
	// them moves a coordinate: an observation is always in source pixels.
	class DetectBoardNode : public flow::Node
	{
	public:
		DetectBoardNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<DetectBoardNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_image;		   // "image" (image::Image)
		flow::PortId m_board;		   // "board" (board::Specification)
		flow::PortId m_frame;		   // "frame" (media::FrameRef), optional
		flow::PortId m_longestSide;	   // "longestSide" (int, 0 = native)
		flow::PortId m_refineNative;   // "refineNative" (bool)
		flow::PortId m_minimumCorners; // "minimumCorners" (int)
		flow::PortId m_report;		   // "report" (board::DetectionReport)
	};
} // namespace lain::camera
