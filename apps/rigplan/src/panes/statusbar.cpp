#include "statusbar.h"

#include "../appcontext.h"

#include <lain/gui/dock.h> // beginStatusBar / endStatusBar: the viewport side bar
#include <lain/gui/gui.h>
#include <lain/rigplan/rigplan.h>
#include <lain/string/format.h>

#include <string>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	void StatusBarPane::draw(AppContext& ctx)
	{
		if (!gui::beginStatusBar())
			return;
		const rp::RigPlan& plan = ctx.document.plan;
		std::string text = string::format("{} mounts · {} cameras · {} lights · {} frames", plan.mounts.size(), plan.cameras.size(),
										  plan.lights.size(), plan.timeline.frames);
		if (plan.timeline.rate.specified())
			text += string::format(" at {}", plan.timeline.rate.toString());
		const std::size_t problems = rp::check(plan).size();
		if (problems > 0)
			text += string::format(" · {} problem{}", problems, problems == 1 ? "" : "s");
		if (ctx.messageFrames > 0)
			text += " · " + ctx.message;
		gui::TextUnformatted(text.c_str());
		gui::endStatusBar();
	}
} // namespace rigplan
