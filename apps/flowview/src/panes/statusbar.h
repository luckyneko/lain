#pragma once

namespace flowview
{
	struct AppContext;

	// The status bar across the bottom of the window (M14 slice 6, ADR-0025): how runs are being
	// started, which scheduler runs them, and what the run in flight — or the last one — is doing.
	//
	//     Live · Parallel · Computing 12/40 · 3.4 s [Stop]
	//     Manual · Serial · Last run 3.4 s · 3 stale here - Run (Cmd+Enter)
	//
	// On the viewport rather than in a pane, so it is visible whatever the dock layout and whichever
	// tab is in front. Stateless: everything it shows is the app's or the AppContext's.
	struct StatusBarPane
	{
		void draw(AppContext& ctx);
	};
} // namespace flowview
