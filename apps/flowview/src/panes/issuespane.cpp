#include "issuespane.h"

#include "../appcontext.h"
#include "../groupnav.h" // GraphPath — a map element's row navigates INTO it

#include <lain/flow/boundary.h> // the inner GroupOutput a map element delivers to
#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/gui/color.h> // gui::packColor (image::ColorRGBA8 -> ImU32)
#include <lain/gui/gui.h>
#include <lain/image/color.h>
#include <lain/string/format.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace flowview
{
	using namespace lain;

	// Live validation of the current graph: a required input with no incoming edge (can't run), and an
	// active node's output with no outgoing edge (a dead end). Recomputed each frame — cheap at
	// prototyping scale, and self-clearing as the graph is fixed.
	static std::vector<Issue> collectIssues(const flow::Graph& graph, const flow::Evaluation& evaluation,
											const GraphPath& activePath)
	{
		std::vector<Issue> issues;
		// The ports an edge touches, by their durable addresses — the same key an edge stores.
		std::unordered_set<flow::PortAddress> connectedIn;
		std::unordered_set<flow::PortAddress> connectedOut;
		for (const flow::Graph::Edge& e : graph.edges())
		{
			connectedIn.insert(e.to);
			connectedOut.insert(e.from);
		}
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			// A row names the node by title plus a truncated id — enough to tell two same-titled
			// nodes apart, where the full uuid would bury the message.
			const std::string label = id.shortString();
			for (std::size_t i = 0; i < node.inputCount(); ++i)
			{
				const flow::Port& in = node.input(i);
				if (in.required() && connectedIn.count({id, in.id()}) == 0)
				{
					issues.push_back(Issue::at(Issue::Severity::Warning,
											   string::format("{} [{}]: required input '{}' is not connected", node.name(), label, in.name()),
											   id));
				}
			}
			if (evaluation.ready(id))
			{
				for (std::size_t o = 0; o < node.outputCount(); ++o)
				{
					const flow::Port& out = node.output(o);
					if (connectedOut.count({id, out.id()}) == 0)
					{
						issues.push_back(Issue::at(Issue::Severity::Info,
												   string::format("{} [{}]: output '{}' is unused", node.name(), label, out.name()),
												   id));
					}
				}
			}

			// A MAP whose gather found a HOLE. One element producing nothing clears the WHOLE output
			// (ADR-0014: a vector has no hole, and shortening it would break the correspondence with
			// the input), so without this the user sees an empty result and no reason for it.
			//
			// The engine reports no element index itself — it has no channel to, and flow core stays
			// log-free — but the child evaluations are right here, so the host simply looks. One row
			// per output rather than one per failed element: a systematically broken folder would
			// otherwise bury the panel under a row per file.
			if (node.interiorEvaluation() == flow::InteriorEvaluation::PerElement && node.innerGraph() != nullptr)
			{
				const flow::NodeId boundary = node.innerGraph()->boundaryOutputNode().id();
				const std::size_t elements = evaluation.childCount(id);
				for (std::size_t o = 0; o < node.outputCount(); ++o)
				{
					const flow::Port& out = node.output(o);
					const flow::PortId pin = node.innerPin(out.id());
					if (pin == flow::PortId{})
						continue;

					std::size_t missing = 0;
					std::size_t first = 0;
					for (std::size_t element = 0; element < elements; ++element)
					{
						if (!evaluation.child(id, element).value(flow::PortAddress{boundary, pin}).empty())
							continue;
						if (missing == 0)
							first = element;
						++missing;
					}
					if (missing == 0)
						continue;

					GraphPath into = activePath;
					into.push_back(PathStep{id, first});
					// `inside`, not `at`: the subject is the ELEMENT's level, one step down, and there is
					// no node at this level worth centring — the map itself is plainly visible.
					issues.push_back(Issue::inside(Issue::Severity::Warning,
												   string::format("{} [{}]: {} of {} elements produced no '{}' — the whole output is cleared (first: element {})",
																  node.name(), label, missing, elements, out.name(), first + 1),
												   std::move(into)));
				}
			}
		}
		return issues;
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
