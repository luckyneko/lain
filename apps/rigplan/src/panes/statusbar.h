#pragma once

namespace rigplan
{
	struct AppContext;

	// The bar across the bottom of the window: what the plan holds, its timeline, how many problems
	// check() has with it, and the moment's message.
	struct StatusBarPane
	{
		void draw(AppContext& ctx);
	};
} // namespace rigplan
