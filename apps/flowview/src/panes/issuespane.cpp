#include "issuespane.h"

#include "../appcontext.h"

#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/color.h> // gui::packColor (image::ColorRGBA8 -> ImU32)
#include <lain/gui/gui.h>
#include <lain/image/color.h>
#include <lain/string/format.h>

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// Live validation of the current graph: a required input with no incoming edge (can't run), and an
	// active node's output with no outgoing edge (a dead end). Recomputed each frame — cheap at
	// prototyping scale, and self-clearing as the graph is fixed.
	static std::vector<Issue> collectIssues(const flow::Graph& graph)
	{
		std::vector<Issue> issues;
		std::set<std::pair<std::uint64_t, std::uint32_t>> connectedIn;
		std::set<std::pair<std::uint64_t, std::uint32_t>> connectedOut;
		for (const flow::Graph::Edge& e : graph.edges())
		{
			connectedIn.insert({e.to.node.value(), e.to.port.value()});
			connectedOut.insert({e.from.node.value(), e.from.port.value()});
		}
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			for (flow::PortIndex i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& in = node.input(i);
				if (in.required() && connectedIn.count({id.value(), in.id().value()}) == 0)
				{
					issues.push_back({Issue::Severity::Warning,
									  string::format("{} [{}]: required input '{}' is not connected", node.name(), id.value(), in.name()),
									  id});
				}
			}
			if (node.ready())
			{
				for (flow::PortIndex o = 0; o < node.outputCount(); ++o)
				{
					const flow::Port& out = node.output(o);
					if (connectedOut.count({id.value(), out.id().value()}) == 0)
					{
						issues.push_back({Issue::Severity::Info,
										  string::format("{} [{}]: output '{}' is unused", node.name(), id.value(), out.name()),
										  id});
					}
				}
			}
		}
		return issues;
	}

	void IssuesPane::draw(AppContext& ctx, const flow::Graph& graph)
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
				if (gui::Selectable(issue.message.c_str()) && issue.node != flow::NodeId{})
					ctx.locateNode(issue.node);
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
			const std::vector<Issue> derived = collectIssues(graph);
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
