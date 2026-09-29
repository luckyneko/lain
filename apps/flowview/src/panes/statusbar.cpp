#include "statusbar.h"

#include "../appcontext.h"
#include "../flowviewapp.h" // the run in flight, the last run, the scheduler

#include <lain/gui/dock.h> // beginStatusBar / endStatusBar — the viewport side bar
#include <lain/gui/gui.h>
#include <lain/string/format.h>

#include <cstddef>
#include <string>

namespace flowview
{
	using namespace lain;

	static const char* triggerName(RunTrigger trigger)
	{
		switch (trigger)
		{
			case RunTrigger::Live:
				return "Live";
			case RunTrigger::OnCommit:
				return "On commit";
			case RunTrigger::Manual:
				return "Manual";
		}
		return "";
	}

	static const char* strategyName(RunStrategy strategy)
	{
		switch (strategy)
		{
			case RunStrategy::Parallel:
				return "Parallel";
			case RunStrategy::Serial:
				return "Serial";
		}
		return "";
	}

	void StatusBarPane::draw(AppContext& ctx)
	{
		if (!gui::beginStatusBar())
			return;

		const FlowviewApp& app = *ctx.app;
		std::string text = string::format("{} · {}", triggerName(ctx.options.trigger), strategyName(app.strategy()));

		if (app.running())
		{
			// Node computes, summed across stages — so the total grows as a map's elements and a loop's
			// iterations are planned, which is why it is a count and not a bar.
			text += string::format(" · Computing {}/{} · {:.1f} s", app.runFinished(), app.runPlanned(), app.runElapsed());
		}
		else if (const std::optional<LastRun>& last = app.lastRun())
		{
			// A Run Selection says so: what it did not reach is still Stale, and "Last run" alone would
			// read as though the whole graph had been brought up to date.
			text += string::format(" · Last run{} {:.1f} s", last->partial ? " (selection)" : "", last->seconds);
			if (last->outcome == RunOutcome::Cancelled)
				text += " (stopped)";
			else if (last->outcome == RunOutcome::Failed)
				text += last->failedNodes > 0 ? string::format(" ({} failed)", last->failedNodes) : std::string(" (failed)");
		}

		// What the level on screen shows, as the canvas marks it. Under Manual a stale node waits for
		// Run, so the bar says how to get one.
		const std::size_t stale = ctx.freshness.count(Freshness::Stale);
		const std::size_t failed = ctx.freshness.count(Freshness::Failed);
		if (failed > 0)
			text += string::format(" · {} failed here", failed);
		if (stale > 0)
		{
			text += string::format(" · {} stale here", stale);
			if (ctx.options.trigger == RunTrigger::Manual && !app.running())
				text += string::format(" - Run ({})", gui::GetIO().ConfigMacOSXBehaviors ? "Cmd+Enter" : "Ctrl+Enter");
		}

		gui::TextUnformatted(text.c_str());
		if (app.running())
		{
			gui::SameLine();
			if (gui::SmallButton("Stop"))
				ctx.app->stopRun();
		}
		gui::endStatusBar();
	}
} // namespace flowview
