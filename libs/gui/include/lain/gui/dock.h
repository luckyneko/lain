#pragma once

#include <cstdint>

// The docking seam over ImGui — so a client can build a dock layout without reaching into ImGui's
// internal DockBuilder header (imgui_internal.h). DockSpaceOverViewport is public ImGui, but the
// DockBuilder layout API is internal; this fronts both with lain types.

namespace lain::gui
{
	// An ImGui dock-node id, opaque to callers.
	using DockNode = std::uint32_t;

	enum class DockDir
	{
		Left,
		Right,
		Up,
		Down,
	};

	// The dockspace filling the main viewport (below the menu bar); call once per frame before the
	// panels so their windows dock into it. Returns its node.
	DockNode dockSpaceOverViewport();

	// The result of splitting a node: `first` is the child on the `dir` side (taking `fraction` of the
	// space), `second` is the remainder.
	struct DockSplit
	{
		DockNode first;
		DockNode second;
	};

	// Stamp a layout into a dockspace (hides the internal DockBuilder API): reset it to a fresh root
	// sized to the viewport, split nodes, dock windows into nodes by name, then finish. For a first-run
	// / reset default; a saved .ini otherwise wins.
	void dockReset(DockNode dockspace);
	DockSplit dockSplit(DockNode node, DockDir dir, float fraction);
	void dockWindow(DockNode node, const char* windowName);
	void dockFinish(DockNode dockspace);

	// Bring a docked window's tab to the front — programmatic tab activation (e.g. routing an asset to
	// a Preview pane). Drives the tab bar's own selection (NextSelectedTabId), which is more reliable
	// than SetWindowFocus for a docked background tab. No-op if the window isn't found; plain focus if
	// it isn't docked in a tab bar.
	void activateWindowTab(const char* windowName);
} // namespace lain::gui
