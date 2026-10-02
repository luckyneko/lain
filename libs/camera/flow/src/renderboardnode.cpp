#include "lain/camera/flow/renderboardnode.h"

#include "reason.h"

#include <algorithm>
#include <cstdint>

namespace lain::camera
{
	RenderBoardNode::RenderBoardNode()
		: Node("RenderBoard")
	{
		m_board = addInput<board::Specification>("board");
		m_pixelsPerSquare = addParam<int>("pixelsPerSquare", 100);
		m_margin = addParam<int>("marginPixels", 0);

		m_image = addOutput<image::Image>("image");
		m_description = addOutput<std::string>("description");
	}

	void RenderBoardNode::compute(flow::NodeEvaluation& evaluation) const
	{
		const flow::PortValue& slot = evaluation.input(m_board);
		if (slot.empty())
		{
			evaluation.output(m_image).clear();
			evaluation.output(m_description).clear();
			return;
		}

		board::RenderRequest request;
		request.pixelsPerSquare = std::uint32_t(std::max(0, param(m_pixelsPerSquare).get<int>()));
		request.marginPixels = std::uint32_t(std::max(0, param(m_margin).get<int>()));

		board::RenderResult result = board::render(slot.get<board::Specification>().pattern(), request);
		if (!result.rendering)
		{
			// A failure the gui shows on this node, not only a log line: a build with no renderer
			// still loads and runs a board document (ADR-0016), and this is where it says it cannot.
			evaluation.fail(detail::reason(result.diagnostics));
			return;
		}
		evaluation.output(m_image).set(std::move(result.rendering->raster));
		evaluation.output(m_description).set(std::move(result.rendering->description));
	}
} // namespace lain::camera
