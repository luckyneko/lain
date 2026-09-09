#include "issuespane.h"

#include "../appcontext.h"
#include "../groupnav.h"   // GraphPath (a map element's row navigates INTO it) + PinRefusal
#include "../validation.h" // collectIssues — the rules themselves, apart from the drawing

#include <lain/gui/color.h> // gui::packColor (image::ColorRGBA8 -> ImU32)
#include <lain/gui/gui.h>
#include <lain/image/color.h>
#include <lain/string/format.h>

#include <string>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// A pin a group could not mirror onto its own face. The row states the CONSEQUENCE rather than the
	// cause, because there are two causes — a loop's name collision, a map's unliftable type — and the
	// sync reports the pin by name without saying which (edit::GroupSync::refused). What they share is
	// what the user is looking at: an inner pin with no port outside, and no other symptom at all.
	//
	// It leads to the level the pin LIVES on, so the Interface pane there can rename it — unless that
	// is the level already drawn, where a click would spend a preview-cache clear going nowhere.
	static Issue refusalRow(const PinRefusal& refusal, const GraphPath& drawnPath)
	{
		std::string message = string::format("{} [{}]: inner pin '{}' could not be mirrored onto its face — nothing outside can connect to it",
											 refusal.group, refusal.node.shortString(), refusal.pin);
		if (refusal.level == drawnPath)
			return Issue::note(Issue::Severity::Warning, std::move(message));
		return Issue::inside(Issue::Severity::Warning, std::move(message), refusal.level);
	}

	void IssuesPane::draw(AppContext& ctx, const flow::Graph& graph, const flow::Evaluation& evaluation)
	{
		if (gui::Begin("Issues"))
		{
			const auto severityColour = [](Issue::Severity s) -> image::ColorRGBA8
			{
				switch (s)
				{
					case Issue::Severity::Info:
						return image::ColorRGBA8(150, 150, 160, 255);
					case Issue::Severity::Warning:
						return image::ColorRGBA8(230, 180, 60, 255);
					case Issue::Severity::Error:
						return image::ColorRGBA8(230, 90, 80, 255);
				}
				return image::ColorRGBA8(200, 200, 200, 255);
			};
			int idx = 0;
			const auto row = [&](const Issue& issue)
			{
				gui::PushID(idx++); // duplicate messages would otherwise share a Selectable id
				gui::PushStyleColor(ImGuiCol_Text, gui::packColor(severityColour(issue.severity)));
				if (gui::Selectable(issue.message.c_str()) && issue.locatable())
				{
					// A row that names something at ANOTHER level takes you there first — otherwise
					// "element 7 produced nothing" is a statement with nowhere to act on it.
					if (!issue.navigateTo.empty())
						ctx.navigateTo(issue.navigateTo);
					else
						ctx.locateNode(issue.node);
				}
				gui::PopStyleColor();
				gui::PopID();
			};

			bool any = false;
			if (ctx.recentIssueFrames > 0 && ctx.recentIssue)
			{
				row(*ctx.recentIssue);
				--ctx.recentIssueFrames; // transient: fade after a few seconds
				any = true;
			}
			for (const Issue& issue : ctx.loadIssues)
			{
				row(issue);
				any = true;
			}
			for (const PinRefusal& refusal : ctx.syncRefusals)
			{
				row(refusalRow(refusal, ctx.drawnPath));
				any = true;
			}
			const std::vector<Issue> derived = collectIssues(graph, evaluation, ctx.activePath);
			for (const Issue& issue : derived)
			{
				row(issue);
				any = true;
			}
			if (!any)
				gui::TextDisabled("No issues.");
		}
		gui::End();
	}
} // namespace flowview
