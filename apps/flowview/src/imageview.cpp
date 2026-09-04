#include "imageview.h"

#include <lain/gui/gui.h>
#include <lain/gui/texture.h>

namespace flowview
{
	using namespace lain;

	// The value itself is unused: an image's poster IS the image, so everything this needs — the
	// pixels and their aspect — is already on the uploaded texture. A view that reached back into the
	// value for its size would be asking a second source for a fact the first one holds.
	void ImageView::draw(const flow::PortValue&, const gui::Texture* poster, math::Vec2f area, gui::Context&)
	{
		if (poster == nullptr)
		{
			// The value went away (or its upload failed) between the cache's last refresh and now.
			gui::TextDisabled("(no image to show)");
			return;
		}

		// Reserve the toolbar's height up front so the image area — and therefore the fit scale — is
		// known BEFORE the toolbar draws. Otherwise the percentage it reports would lag a frame behind
		// the size it describes.
		const math::Vec2f image{area.x, area.y - ImageCanvas::toolbarHeight()};
		m_canvas.drawImage("view", *poster, poster->extent(), image);
		m_canvas.drawToolbar();
	}
} // namespace flowview
