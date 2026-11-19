#pragma once

namespace rigplan
{
	struct AppContext;

	// The plan as a tree: the plan, the room, its specifications by kind, its mounts with the cameras
	// and lights on each, its planes, boxes and props, and its boards, a mover listing its keys. A click
	// selects, and a row whose object check() has a problem with is marked, its problems in a tooltip.
	// A camera or light on a mount the plan lacks is listed under "Not on a mount", so it can still be
	// selected and given one.
	struct OutlinerPane
	{
		void draw(AppContext& ctx);
	};
} // namespace rigplan
