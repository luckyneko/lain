#pragma once

#include "imagecanvas.h"
#include "valueviews.h"

#include <lain/gui/texture.h>	 // the decoded current frame (a member)
#include <lain/media/frameref.h> // which frame that texture IS (a member)

#include <cstddef>

namespace lain::media
{
	class FrameSequence;
}

namespace flowview
{
	// The view for a media::FrameSequence: a player over the frames, so footage on a pin can be
	// looked at rather than read as "500 frames · 3840x2160 RGB8 BT709 · 24 fps".
	//
	// AN INSPECTION PLAYER, NOT A TIMELINE (ADR-0018, and WORK.md M10's "not in this milestone").
	// It decodes straight out of the value on the pin and never re-runs the graph — so it shows the
	// footage a node is holding, not the graph's output at that frame. Driving a render is binding a
	// FramePosition boundary input, which is the Interface pane's editor and, later, a transport
	// that does it for you; FramePosition being a distinct type is what keeps that a one-widget
	// change rather than a rewrite of this.
	//
	// It owns its decoded frame, unlike ImageView, which draws the poster the preview cache already
	// uploaded: the cache is rebuilt on an EDIT, and playback advances with no edit at all.
	class SequenceView : public ValueView
	{
	public:
		void draw(const lain::flow::PortValue& value, const lain::gui::Texture* poster,
				  lain::math::Vec2f area, lain::gui::Context& gui) override;

	private:
		// Decode `position` into m_texture (uploading in place when the extent/format match). Records
		// the frame it attempted EITHER WAY, so a frame that will not decode is not retried on every
		// frame — which at 60fps would be a decode storm on a file that is simply broken.
		void showFrame(const lain::media::FrameSequence& sequence, std::size_t position, lain::gui::Context& gui);

		// Advance the position by however much wall time has passed, at the sequence's own rate.
		// Returns the position to show. Whole frames are SKIPPED when decoding cannot keep up, so
		// playback tracks the clock rather than sliding behind it — ADR-0018's "best effort".
		void advancePlayback(double rate, std::size_t last);

		ImageCanvas m_canvas;
		lain::gui::Texture m_texture; // the decoded frame named by m_shownFrame
		// WHICH FRAME that texture is, by identity rather than by position — a FrameRef, which
		// sequence.frame() answers without decoding. Position alone would not do: a re-run can rebind
		// a DIFFERENT sequence onto the pin while the transport sits on the same position, and the
		// view would go on showing the old footage. Invalid means nothing has been decoded yet.
		lain::media::FrameRef m_shownFrame;
		std::size_t m_position = 0; // which position the transport is on
		bool m_playing = false;
		double m_carry = 0.0; // seconds accumulated toward the next frame
	};
} // namespace flowview
