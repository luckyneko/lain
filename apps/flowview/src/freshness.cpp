#include "freshness.h"

#include "runner.h" // RunActivity

#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/staleness.h>
#include <lain/gui/color.h> // packColor
#include <lain/gui/gui.h>
#include <lain/image/color.h>
#include <lain/math/types.h>

#include <algorithm>

namespace flowview
{
	using namespace lain;

	Freshness LevelFreshness::of(flow::NodeId id) const
	{
		const auto it = nodes.find(id);
		return it == nodes.end() ? Freshness::Stale : it->second;
	}

	std::size_t LevelFreshness::count(Freshness state) const
	{
		std::size_t matching = 0;
		for (const auto& entry : nodes)
		{
			if (entry.second == state)
				++matching;
		}
		return matching;
	}

	// Whether anything inside `id` — in any of its child evaluations, every element of a map — holds a
	// failure. "A group shows the most active state inside it."
	static bool failedInside(const flow::Evaluation& evaluation, flow::NodeId id)
	{
		for (std::size_t i = 0; i < evaluation.childCount(id); ++i)
		{
			if (evaluation.child(id, i).hasFailure())
				return true;
		}
		return false;
	}

	// What the run in flight is doing at or under each node of the level `path` names: a node's own
	// entry, or any entry deeper down through it — `path + {node, any element} + ...` — since a group,
	// map or loop shows the most active state inside it. Computing outranks Queued. One pass over what
	// the run reported, however deep it goes.
	static std::map<flow::NodeId, Activity> activityAt(const RunActivity& activity, const flow::EvalPath& path)
	{
		std::map<flow::NodeId, Activity> level;
		for (const auto& entry : activity.nodes)
		{
			const flow::EvalPath& where = entry.first.first;
			if (where.size() < path.size() || !std::equal(path.begin(), path.end(), where.begin()))
				continue;
			const flow::NodeId node = (where.size() == path.size()) ? entry.first.second : where[path.size()].node;
			const auto placed = level.emplace(node, entry.second);
			if (!placed.second && entry.second == Activity::Computing)
				placed.first->second = Activity::Computing;
		}
		return level;
	}

	LevelFreshness levelFreshness(const flow::Graph& document, const flow::Evaluation& published, const flow::EvalPath& path,
								  const RunActivity& activity)
	{
		// What a level is compared against when nothing has run in it: no records, so every node there
		// is stale. Never prepared, so it pairs with any definition.
		static const flow::Evaluation nothing;

		const flow::Graph* graph = &document;
		const flow::Evaluation* evaluation = &published;
		bool boundaryStale = false; // the root's boundary is handed values only by a binding, which requests it itself
		flow::EvalPath resolved;	// the level actually reached, which is what the run's reports are matched against
		for (const flow::EvalStep& step : path)
		{
			// The same truncation as resolvePath: a step that is gone, or no longer contains a graph,
			// ends the walk at the deepest level that did resolve.
			if (!graph->contains(step.node))
				break;
			const flow::Graph* inner = graph->node(step.node).innerGraph();
			if (inner == nullptr)
				break;
			resolved.push_back(step);

			// Whether the level below is being handed values its evaluation has not seen: the level
			// above says so, by the interior's kind — the one thing a per-level comparison cannot see.
			boundaryStale = flow::StaleClosure(*graph, *evaluation, boundaryStale).reseeds(step.node);
			evaluation = evaluation->hasChild(step.node, step.index) ? &evaluation->child(step.node, step.index) : &nothing;
			graph = inner;
		}

		const flow::StaleClosure closure(*graph, *evaluation, boundaryStale);
		const std::map<flow::NodeId, Activity> active = activityAt(activity, resolved);
		LevelFreshness level;
		for (const flow::NodeId id : graph->nodeIds())
		{
			Freshness state = Freshness::Current;
			if (evaluation->failure(id) != nullptr || failedInside(*evaluation, id))
				state = Freshness::Failed;
			else if (closure.contains(id))
				state = Freshness::Stale;

			// The run's half, by the one order (freshness.h): Computing over everything, Queued over
			// all but Failed.
			const auto doing = active.find(id);
			if (doing != active.end())
			{
				if (doing->second == Activity::Computing)
					state = Freshness::Computing;
				else if (state != Freshness::Failed)
					state = Freshness::Queued;
			}
			level.nodes.emplace(id, state);
		}
		return level;
	}

	//=========================================================================
	// Drawing
	//=========================================================================

	// The two marks' colours, in one place because they appear on two surfaces (a canvas title bar and
	// a thumbnail). Stale is light and quiet — it is ordinary under Manual, not a problem; Failed is the
	// Issues pane's error red.
	static const image::ColorRGBA8 kStaleMark(228, 228, 238, 230);
	static const image::ColorRGBA8 kComputingMark(120, 190, 255, 255); // the canvas' computing outline, near enough
	static const image::ColorRGBA8 kFailedMark(230, 90, 80, 255);
	static const image::ColorRGBA8 kFailedText(255, 255, 255, 255);
	static const image::ColorRGBA8 kBadgeBacking(20, 20, 24, 200); // so a mark reads over any picture

	void drawFreshnessMark(float x, float y, float radius, Freshness state)
	{
		ImDrawList* draw = gui::GetWindowDrawList();
		const math::Vec2f centre{x, y};
		switch (state)
		{
			case Freshness::Current:
				return;
			case Freshness::Stale:
				draw->AddCircle(centre, radius, gui::packColor(kStaleMark), 0, 1.5f);
				return;
			case Freshness::Queued:
				draw->AddCircleFilled(centre, radius * 0.75f, gui::packColor(kStaleMark));
				return;
			case Freshness::Computing:
			{
				// Three quarters of a ring, turning: it moves, which a node whose value is on its way
				// should, and no static state does. The frame loop redraws continuously, so it animates.
				constexpr float kTurnsPerSecond = 0.8f;
				constexpr float kTwoPi = 6.2831853f;
				const float start = static_cast<float>(gui::GetTime()) * kTurnsPerSecond * kTwoPi;
				draw->PathArcTo(centre, radius, start, start + kTwoPi * 0.75f, 16);
				draw->PathStroke(gui::packColor(kComputingMark), 0, 1.8f);
				return;
			}
			case Freshness::Failed:
			{
				draw->AddCircleFilled(centre, radius, gui::packColor(kFailedMark));
				// The "!" at the mark's own size, not the font's, so it stays inside the disc.
				const float size = radius * 1.7f;
				const math::Vec2f extent = gui::GetFont()->CalcTextSizeA(size, 1000.0f, 0.0f, "!");
				draw->AddText(gui::GetFont(), size, math::Vec2f{x - extent.x * 0.5f, y - extent.y * 0.5f},
							  gui::packColor(kFailedText), "!");
				return;
			}
		}
	}

	void drawFreshnessBadge(Freshness state)
	{
		if (state == Freshness::Current)
			return;
		const math::Vec2f corner = gui::GetItemRectMin();
		const float radius = gui::GetFontSize() * 0.45f;
		const float inset = radius + 4.0f;
		gui::GetWindowDrawList()->AddCircleFilled(math::Vec2f{corner.x + inset, corner.y + inset}, radius + 2.0f,
												  gui::packColor(kBadgeBacking));
		drawFreshnessMark(corner.x + inset, corner.y + inset, radius, state);

		if (!gui::IsItemHovered())
			return;
		switch (state)
		{
			case Freshness::Current:
				return;
			case Freshness::Stale:
				gui::SetTooltip("Stale: this value predates the latest change");
				return;
			case Freshness::Queued:
				gui::SetTooltip("Queued: the run in flight will recompute this value");
				return;
			case Freshness::Computing:
				gui::SetTooltip("Computing: the run in flight is recomputing this value now");
				return;
			case Freshness::Failed:
				gui::SetTooltip("Failed: this node's last compute failed (see Issues for why)");
				return;
		}
	}
} // namespace flowview
