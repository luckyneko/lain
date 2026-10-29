#include "issues.h"

#include "../appcontext.h"

#include <lain/gui/color.h>
#include <lain/gui/gui.h>
#include <lain/rigplan/rigplan.h>

#include <string>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	static const image::ColorRGBA8 kWarning{230, 180, 60, 255};
	static const image::ColorRGBA8 kError{230, 90, 80, 255};
	static const image::ColorRGBA8 kInfo{150, 150, 160, 255};

	// One row, in its colour; clicking it selects `target` when it names something.
	static void row(AppContext& ctx, int& index, const std::string& text, const image::ColorRGBA8& colour, const Selection& target)
	{
		gui::PushID(index++); // two rows may say the same thing
		gui::PushStyleColor(ImGuiCol_Text, gui::packColor(colour));
		if (gui::Selectable(text.c_str(), target.kind != Selected::Nothing && ctx.selection == target) && target.kind != Selected::Nothing)
			ctx.selection = target;
		gui::PopStyleColor();
		gui::PopID();
	}

	void IssuesPane::draw(AppContext& ctx)
	{
		if (gui::Begin("Issues"))
		{
			int index = 0;
			bool any = false;
			if (ctx.messageFrames > 0)
			{
				row(ctx, index, ctx.message, ctx.messageIsError ? kError : kInfo, Selection::nothing());
				--ctx.messageFrames;
				any = true;
			}
			for (const std::string& problem : ctx.openProblems)
			{
				row(ctx, index, problem, kError, Selection::nothing());
				any = true;
			}
			for (const std::string& problem : ctx.assetProblems)
			{
				row(ctx, index, problem, kWarning, Selection::nothing());
				any = true;
			}
			for (const rp::PlanDiagnostic& d : rp::check(ctx.document.plan))
			{
				const Selection target = d.owner ? selectionOf(ctx.document.plan, *d.owner) : Selection::nothing();
				row(ctx, index, d.detail, kWarning, target);
				any = true;
			}
			if (!any)
				gui::TextDisabled("No issues.");
		}
		gui::End();
	}
} // namespace rigplan
