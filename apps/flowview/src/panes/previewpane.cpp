#include "previewpane.h"

#include "../appcontext.h"
#include "../previewcache.h"

#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/dock.h> // activateWindowTab
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/math/types.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace flowview
{
	using namespace lain;

	// Zoom bounds. The floor is min(fit, 1.0) so ACTUAL SIZE is always reachable: for an image larger
	// than the pane, fit is below 1 and 1:1 is a zoom in; for one smaller, fit magnifies it and zooming
	// out bottoms out at 1:1. Either way you can never zoom out past "fit or actual, whichever is
	// smaller", and 1:1 is always somewhere on the slider.
	static constexpr float kMaxZoom = 16.0f;
	static constexpr float kWheelStep = 1.15f;

	// The Preview window contents (no Begin/End — the caller owns those). Early-returns freely.
	void PreviewPane::drawContents(AppContext& ctx, const flow::Graph& graph, const flow::Evaluation& evaluation, const PreviewCache& previews)
	{
		if (!ctx.previewTarget)
		{
			gui::TextDisabled("Click an asset thumbnail to preview it here.");
			return;
		}

		// Resolve the targeted port; drop the target if its node/port is gone or no longer a ready image.
		const PinKey key = *ctx.previewTarget;
		// Resolved against the level the KEY names, not merely the level on screen — the key now says
		// which. This pane only ever holds the active graph, so a target from elsewhere simply does
		// not resolve and is dropped below; what changed is that it can no longer resolve to the
		// WRONG pin by sharing ids with one here.
		const bool sameLevel = key.path == ctx.activePath;
		const flow::Node* node = (sameLevel && graph.contains(key.port.node)) ? &graph.node(key.port.node) : nullptr;
		const flow::Port* port = node != nullptr ? node->findOutput(key.port.port) : nullptr;
		if (node != nullptr && port == nullptr)
			port = node->findInput(key.port.port);
		const gui::Texture* tex = previews.find(key);
		const flow::PortValue& value = (node != nullptr && port != nullptr) ? evaluation.value(key.port) : flow::PortValue{};
		if (node == nullptr || port == nullptr || value.empty() || port->type() != typeid(image::Image) || tex == nullptr)
		{
			ctx.previewTarget.reset();
			gui::TextDisabled("(the previewed asset is no longer available)");
			return;
		}

		// Header: what you are looking at (which node's which pin, and the image size). ASCII only —
		// the default font has no fancy separators.
		const image::Image& img = value.get<image::Image>();
		gui::Text("%s [%s]  |  %s : %s  (%dx%d)", node->name().c_str(), key.port.node.shortString().c_str(),
				  port->name().c_str(), std::string(port->typeName()).c_str(), img.width(), img.height());
		gui::Separator();

		// A different asset gets a fresh view: its size (and so its fit) has nothing to do with the
		// last one's.
		if (m_shownKey != key)
		{
			m_shownKey = key;
			m_fitMode = true;
		}

		// Reserve the toolbar's height up front so the image area — and therefore the fit scale — is
		// known BEFORE the toolbar draws. Otherwise the percentage it reports would lag a frame behind
		// the size it describes.
		const math::Vec2f avail = gui::GetContentRegionAvail();
		const math::Vec2f area{avail.x, avail.y - gui::GetFrameHeightWithSpacing()};
		if (area.x <= 0.0f || area.y <= 0.0f)
			return;

		const math::Vec2f fitted = previewFit(img.extent(), area); // the same fit the thumbnails use
		if (fitted.x <= 0.0f)
			return;
		const float fitScale = fitted.x / static_cast<float>(img.width());
		const float minZoom = std::min(fitScale, 1.0f);
		if (m_fitMode)
			m_zoom = fitScale;
		m_zoom = std::clamp(m_zoom, minZoom, kMaxZoom);

		// The image lives in a child so it is clipped and scrollable. NoScrollWithMouse keeps the wheel
		// for zooming; panning is a drag, which is the image-viewer convention (the graph canvas uses
		// Alt+drag because a plain drag there means box-select — here nothing else wants it).
		gui::BeginChild("view", area, 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			const math::Vec2f draw{static_cast<float>(img.width()) * m_zoom, static_cast<float>(img.height()) * m_zoom};
			// Centre whatever axis has room to spare; the other is scrolled.
			const math::Vec2f slack = gui::GetContentRegionAvail();
			gui::SetCursorPos(math::Vec2f{gui::GetCursorPosX() + std::max(0.0f, (slack.x - draw.x) * 0.5f),
										  gui::GetCursorPosY() + std::max(0.0f, (slack.y - draw.y) * 0.5f)});
			gui::Image(*tex, draw);

			const bool overView = gui::IsWindowHovered();
			if (overView && gui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				const ImVec2 delta = gui::GetIO().MouseDelta;
				gui::SetScrollX(gui::GetScrollX() - delta.x);
				gui::SetScrollY(gui::GetScrollY() - delta.y);
			}
			if (const float wheel = gui::GetIO().MouseWheel; overView && wheel != 0.0f)
			{
				// Anchor the zoom on the cursor: the pixel under the pointer stays under it. Without
				// this, zooming in walks the view away from whatever you were looking at.
				const float previous = m_zoom;
				m_zoom = std::clamp(m_zoom * std::pow(kWheelStep, wheel), minZoom, kMaxZoom);
				m_fitMode = false;
				const ImVec2 local{gui::GetMousePos().x - gui::GetWindowPos().x, gui::GetMousePos().y - gui::GetWindowPos().y};
				const float ratio = m_zoom / previous;
				gui::SetScrollX((gui::GetScrollX() + local.x) * ratio - local.x);
				gui::SetScrollY((gui::GetScrollY() + local.y) * ratio - local.y);
			}
		}
		gui::EndChild();

		// Toolbar, BELOW the image: the header already carries node/port/dimensions, so putting the
		// controls under the content keeps it to one row of chrome before you see anything — and a
		// full-width slider along the bottom reads as a scrubber, which is what it is. (Its height was
		// reserved out of `area` above, so this is a reorder, not a re-layout.) The two zooms worth
		// naming, then a free one; the slider is LOGARITHMIC because zoom is multiplicative and a
		// linear one spends most of its travel in the high end. An edit here lands on the NEXT frame's
		// image, which is invisible during a continuous drag.
		if (gui::Button("Fit"))
		{
			m_fitMode = true;
			m_zoom = fitScale;
		}
		gui::SameLine();
		if (gui::Button("1:1"))
		{
			m_fitMode = false;
			m_zoom = 1.0f;
		}
		gui::SameLine();
		gui::SetNextItemWidth(-1.0f);
		// Driven in PERCENT, because the slider's format prints the raw value — a zoom of 1.0 through
		// a "%%" format would read as "1%".
		float percent = m_zoom * 100.0f;
		if (gui::SliderFloat("##zoom", &percent, minZoom * 100.0f, kMaxZoom * 100.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic))
		{
			m_zoom = percent / 100.0f;
			m_fitMode = false;
		}
	}

	void PreviewPane::draw(AppContext& ctx, const flow::Graph& graph, const flow::Evaluation& evaluation, const PreviewCache& previews)
	{
		if (ctx.activatePreview)
		{
			gui::activateWindowTab("Preview"); // flip the Graph/Preview tab group to Preview
			ctx.activatePreview = false;
		}
		if (gui::Begin("Preview"))
			drawContents(ctx, graph, evaluation, previews);
		gui::End();
	}
} // namespace flowview
