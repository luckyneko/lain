#pragma once

#include <lain/camera/board/rendering.h>
#include <lain/camera/board/specification.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/node.h>
#include <lain/image/image.h>

#include <string>

namespace lain::camera
{
	// A board's pattern drawn for printing: an 8-bit grey `image`, and the machine-readable
	// `description` that proves which pattern it is (board::render). The print's physical size is the
	// printer's; the instance's measured square length is not used here.
	//
	// A render that cannot happen (no renderer in this build, 0 pixels per square, a backend that
	// misbehaved) FAILS the node with render()'s reason (NodeEvaluation::fail): both outputs empty,
	// and the reason where a host shows a failure.
	class RenderBoardNode : public flow::Node
	{
	public:
		RenderBoardNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<RenderBoardNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		flow::PortId m_board;			// "board" (board::Specification)
		flow::PortId m_pixelsPerSquare; // "pixelsPerSquare" (int)
		flow::PortId m_margin;			// "marginPixels" (int)
		flow::PortId m_image;			// "image" (image::Image)
		flow::PortId m_description;		// "description" (std::string)
	};
} // namespace lain::camera
