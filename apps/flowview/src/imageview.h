#pragma once

#include "imagecanvas.h"
#include "valueviews.h"

namespace flowview
{
	// The view for a lain::image::Image: the cached thumbnail texture at full size, with zoom and pan.
	// The pane behaviour that used to BE the Preview pane, now one registered view among others.
	//
	// It draws the poster the cache already uploaded rather than uploading its own, because for an
	// image the poster and the full view are the same pixels — the thumbnail is that texture drawn
	// small. Only a view whose content varies without an edit (a player) needs its own texture.
	class ImageView : public ValueView
	{
	public:
		void draw(const lain::flow::PortValue& value, const lain::gui::Texture* poster,
				  lain::math::Vec2f area, lain::gui::Context& gui) override;

	private:
		ImageCanvas m_canvas;
	};
} // namespace flowview
