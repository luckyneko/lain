#pragma once

namespace rigplan
{
	struct AppContext;

	// The capture timeline and what moves along it: a transport (play and pause, Space; the first, the
	// previous, the next and the last frame; loop; Key, K), a ruler that is the scrubber, and a row per
	// mover, a board's or a prop mover's, with its keys as ticks. A click on a tick selects that key, so
	// the frame shown is the key's; a click elsewhere in a row selects the mover and goes to that frame.
	// A key past the timeline, which check() does not report since a capture simply never reaches it, is
	// drawn in a strip at the right end, so it is seen (timelinescale.h).
	//
	// The clock runs whether or not the pane is on screen, so a tab behind another still plays; scrubbing,
	// stepping and selecting a key here pause it, as a gizmo drag does (ViewportPane). Stateless: what it
	// reads and writes is the AppContext.
	struct TimelinePane
	{
		void draw(AppContext& ctx);
	};
} // namespace rigplan
