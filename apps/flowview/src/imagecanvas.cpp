#include "imagecanvas.h"

#include <lain/gui/gui.h>
#include <lain/gui/texture.h>

#include <algorithm>
#include <cmath>

namespace flowview
{
	using namespace lain;

	// Zoom bounds. The floor is min(fit, 1.0) so ACTUAL SIZE is always reachable: for an image larger
	// than the pane, fit is below 1 and 1:1 is a zoom in; for one smaller, fit magnifies it and zooming
	// out bottoms out at 1:1. Either way you can never zoom out past "fit or actual, whichever is
	// smaller", and 1:1 is always somewhere on the slider.
	static constexpr float kMaxZoom = 16.0f;
	static constexpr float kWheelStep = 1.15f;

	math::Vec2f previewFit(math::Vec2i extent, math::Vec2f box)
	{
		const float w = static_cast<float>(extent.x);
		const float h = static_cast<float>(extent.y);
		if (w <= 0.0f || h <= 0.0f || box.x <= 0.0f || box.y <= 0.0f)
			return math::Vec2f{0.0f, 0.0f};
		// The smaller ratio is the one that fits BOTH ways: width-limited for a wide image, and
		// height-limited for a tall one. Drawing at {side, side} (as this used to) squashed every
		// non-square image into a square.
		const float scale = std::min(box.x / w, box.y / h);
		return math::Vec2f{w * scale, h * scale};
	}

	math::Vec2f thumbnailBox()
	{
		return math::Vec2f{gui::GetContentRegionAvail().x, kThumbnailMaxHeight};
	}

	float ImageCanvas::toolbarHeight()
	{
		return gui::GetFrameHeightWithSpacing();
	}

	void ImageCanvas::drawImage(const char* id, const gui::Texture& texture, math::Vec2i extent, math::Vec2f area)
	{
		if (area.x <= 0.0f || area.y <= 0.0f)
			return;

		const math::Vec2f fitted = previewFit(extent, area); // the same fit the thumbnails use
		if (fitted.x <= 0.0f)
			return;
		m_fitScale = fitted.x / static_cast<float>(extent.x);
		m_minZoom = std::min(m_fitScale, 1.0f);
		if (m_fitMode)
			m_zoom = m_fitScale;
		m_zoom = std::clamp(m_zoom, m_minZoom, kMaxZoom);

		// The image lives in a child so it is clipped and scrollable. NoScrollWithMouse keeps the wheel
		// for zooming; panning is a drag, which is the image-viewer convention (the graph canvas uses
		// Alt+drag because a plain drag there means box-select — here nothing else wants it).
		gui::BeginChild(id, area, 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			const math::Vec2f draw{static_cast<float>(extent.x) * m_zoom, static_cast<float>(extent.y) * m_zoom};
			// Centre whatever axis has room to spare; the other is scrolled.
			const math::Vec2f slack = gui::GetContentRegionAvail();
			gui::SetCursorPos(math::Vec2f{gui::GetCursorPosX() + std::max(0.0f, (slack.x - draw.x) * 0.5f),
										  gui::GetCursorPosY() + std::max(0.0f, (slack.y - draw.y) * 0.5f)});
			gui::Image(texture, draw);

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
				m_zoom = std::clamp(m_zoom * std::pow(kWheelStep, wheel), m_minZoom, kMaxZoom);
				m_fitMode = false;
				const ImVec2 local{gui::GetMousePos().x - gui::GetWindowPos().x, gui::GetMousePos().y - gui::GetWindowPos().y};
				const float ratio = m_zoom / previous;
				gui::SetScrollX((gui::GetScrollX() + local.x) * ratio - local.x);
				gui::SetScrollY((gui::GetScrollY() + local.y) * ratio - local.y);
			}
		}
		gui::EndChild();
	}

	void ImageCanvas::drawToolbar()
	{
		// Drawn BELOW the image: the header already carries node/port/dimensions, so putting the
		// controls under the content keeps it to one row of chrome before you see anything — and a
		// full-width slider along the bottom reads as a scrubber, which is what it is. The two zooms
		// worth naming, then a free one; the slider is LOGARITHMIC because zoom is multiplicative and a
		// linear one spends most of its travel in the high end. An edit here lands on the NEXT frame's
		// image, which is invisible during a continuous drag.
		if (gui::Button("Fit"))
		{
			m_fitMode = true;
			m_zoom = m_fitScale;
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
		if (gui::SliderFloat("##zoom", &percent, m_minZoom * 100.0f, kMaxZoom * 100.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic))
		{
			m_zoom = percent / 100.0f;
			m_fitMode = false;
		}
	}
} // namespace flowview
