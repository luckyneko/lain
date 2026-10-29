#pragma once

namespace rigplan
{
	struct AppContext;

	// What is wrong with the open plan, as check() says it, and why the last plan opened was refused,
	// with the moment's message on top. A row about an object (a PlanDiagnostic's owner) selects it.
	// The asset problems join it with the Viewport (slice 8f), which is what loads them.
	struct IssuesPane
	{
		void draw(AppContext& ctx);
	};
} // namespace rigplan
