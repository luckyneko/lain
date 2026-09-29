#include "freshness.h"

#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/staleness.h>
#include <lain/gui/color.h> // packColor
#include <lain/gui/gui.h>
#include <lain/image/color.h>
#include <lain/math/types.h>

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

	LevelFreshness levelFreshness(const flow::Graph& document, const flow::Evaluation& published, const flow::EvalPath& path)
	{
		// What a level is compared against when nothing has run in it: no records, so every node there
		// is stale. Never prepared, so it pairs with any definition.
		static const flow::Evaluation nothing;

		const flow::Graph* graph = &document;
		const flow::Evaluation* evaluation = &published;
		bool boundaryStale = false; // the root's boundary is handed values only by a binding, which requests it itself
		for (const flow::EvalStep& step : path)
		{
			// The same truncation as resolvePath: a step that is gone, or no longer contains a graph,
			// ends the walk at the deepest level that did resolve.
			if (!graph->contains(step.node))
				break;
			const flow::Graph* inner = graph->node(step.node).innerGraph();
			if (inner == nullptr)
				break;

			// Whether the level below is being handed values its evaluation has not seen: the level
			// above says so, by the interior's kind — the one thing a per-level comparison cannot see.
			boundaryStale = flow::StaleClosure(*graph, *evaluation, boundaryStale).reseeds(step.node);
			evaluation = evaluation->hasChild(step.node, step.index) ? &evaluation->child(step.node, step.index) : &nothing;
			graph = inner;
		}

		const flow::StaleClosure closure(*graph, *evaluation, boundaryStale);
		LevelFreshness level;
		for (const flow::NodeId id : graph->nodeIds())
		{
			Freshness state = Freshness::Current;
			if (evaluation->failure(id) != nullptr || failedInside(*evaluation, id))
				state = Freshness::Failed;
			else if (closure.contains(id))
				state = Freshness::Stale;
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

		if (gui::IsItemHovered())
		{
			gui::SetTooltip(state == Freshness::Failed ? "Failed: this node's last compute threw (see Issues)"
													   : "Stale: this value predates the latest change");
		}
	}
} // namespace flowview
