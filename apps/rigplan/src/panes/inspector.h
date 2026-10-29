#pragma once

namespace rigplan
{
	struct AppContext;

	// The selection's editor: every field of what is selected, in the unit a person thinks in
	// (fields.h). A name is renamed through edits.h, by check()'s rules, and a refusal is said, not
	// applied; every other field is written straight into the plan, and any change marks the document
	// changed, so it is a step of history once the gesture ends. A mover's keys are a table, whose rows
	// select a key; a key selected shows its mover's editor with that row marked.
	struct InspectorPane
	{
		void draw(AppContext& ctx);
	};
} // namespace rigplan
