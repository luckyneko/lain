#pragma once

namespace rigplan
{
	struct AppContext;

	// What is wrong with the open plan, as check() says it, why the last plan opened was refused, and
	// which of the Viewport's assets would not load, with the moment's message on top. A row about an
	// object (a PlanDiagnostic's owner) selects it.
	struct IssuesPane
	{
		void draw(AppContext& ctx);
	};
} // namespace rigplan
