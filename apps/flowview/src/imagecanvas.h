#pragma once

#include <lain/math/types.h>

namespace lain::gui
{
	class Texture;
}

namespace flowview
{
	// The tallest a list thumbnail is drawn. A HEIGHT cap is what bounds the size in practice —
	// width follows from the aspect ratio for anything but a panoramic image, which the pane's own
	// width then catches.
	inline constexpr float kThumbnailMaxHeight = 160.0f;

	// Fit `extent` inside `box`, preserving aspect ratio; the size to draw at. The one place that
	// arithmetic lives — a list thumbnail and the Preview pane's fit-to-pane are the same operation
	// with a different box. Scales UP as well as down, so a small image still fills a list row and the
	// rows keep an even rhythm. Zero extent or box yields zero.
	lain::math::Vec2f previewFit(lain::math::Vec2i extent, lain::math::Vec2f box);

	// The box a LIST thumbnail fits into: the current pane's remaining width, capped in height. Call
	// while the target pane is current.
	lain::math::Vec2f thumbnailBox();

	// The zoom / pan / fit widget over one uploaded texture — the image-viewing half of the Preview
	// pane, extracted so more than one view can hold it. The Preview pane grew this behaviour for a
	// lain::image::Image; the frame-sequence player wants exactly the same thing over a texture it
	// decodes itself, and a second copy of cursor-anchored zoom arithmetic is how the two drift.
	//
	// Split in two calls rather than one, because a view owns its own layout: an image view puts the
	// zoom toolbar directly under the image, while a player puts its transport row in between. The
	// caller reserves toolbarHeight() (plus whatever else it draws) out of the available space and
	// hands the remainder to drawImage.
	class ImageCanvas
	{
	public:
		// The image area: `texture` (of `extent` source pixels) drawn at the current zoom inside
		// `area`, clipped and scrollable, with drag panning and cursor-anchored wheel zoom. `id` scopes
		// the child window, so two canvases in one pane keep their own scroll.
		void drawImage(const char* id, const lain::gui::Texture& texture, lain::math::Vec2i extent,
					   lain::math::Vec2f area);

		// Fit / 1:1 / a logarithmic percent slider — one row, drawn after drawImage (it reports the
		// scale that draw computed).
		void drawToolbar();

		// Back to fit mode: a different asset's size has nothing to do with the last one's, so
		// carrying a 12x zoom onto it would be jarring.
		void refit() { m_fitMode = true; }

		// The height drawToolbar() occupies — reserve it before drawImage, so the fit scale is known
		// BEFORE the toolbar draws and the percentage it reports never lags the size it describes.
		static float toolbarHeight();

	private:
		// `fitMode` is a MODE, not a zoom value: while it holds, the view re-fits as the pane is
		// resized, and it releases the moment the user zooms deliberately (Fit re-enters it).
		float m_zoom = 1.0f;
		bool m_fitMode = true;
		// Computed by drawImage from the area it was given, read by drawToolbar.
		float m_fitScale = 1.0f;
		float m_minZoom = 1.0f;
	};
} // namespace flowview
