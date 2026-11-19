#pragma once

namespace rigplan
{
	struct AppContext;

	// What is wrong with the open plan, as check() says it, why the last plan opened was refused, which
	// of the plan's assets would not load and why the Viewport could not draw it, with the moment's
	// message on top. A row about an object (a PlanDiagnostic's owner) selects it.
	struct IssuesPane
	{
		void draw(AppContext& ctx);
	};
} // namespace rigplan
