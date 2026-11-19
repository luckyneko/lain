#include "timeline.h"

#include "../appcontext.h"
#include "../timelinescale.h"

#include <lain/gui/color.h>
#include <lain/gui/gui.h>
#include <lain/gui/keys.h>
#include <lain/rigplan/rigplan.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	// What the timeline plays at when the plan states no rate, as flowview's sequence player does: a
	// viewing default only, and the readout says so.
	static constexpr double kUnspecifiedRateHz = 24.0;
	// The strip at the right end of the track that keys past the timeline are drawn in, in points.
	static constexpr double kOverflowWidth = 28.0;
	// How far from a tick a click still picks it, in points, when a frame's slot is narrower than this.
	static constexpr double kTickRadius = 6.0;
	// The Outliner's colour for something check() has a problem with: a key past the timeline is one in
	// all but name.
	static const image::ColorRGBA8 kBeyondColour{230, 180, 60, 255};

	// One row of the track: a mover, and the path its keys are on.
	struct MoverRow
	{
		Selection mover;
		std::string name;
		const rp::Path* path = nullptr;
	};

	// Every mover in `plan`, boards first, as the Outliner lists them.
	static std::vector<MoverRow> moverRows(const rp::RigPlan& plan)
	{
		std::vector<MoverRow> rows;
		for (const rp::BoardMover& board : plan.boards)
			rows.push_back({Selection::object(Selected::Board, board.id), board.name, &board.path});
		for (const rp::Prop& prop : plan.props)
		{
			if (prop.path)
				rows.push_back({Selection::object(Selected::Prop, prop.id), prop.name, &*prop.path});
		}
		return rows;
	}

	// Scrubbing and stepping take over from playback: someone steering the frame would have it snatched
	// back a frame later otherwise.
	static void goTo(AppContext& ctx, std::size_t frame)
	{
		ctx.playback.pause();
		ctx.goToFrame(frame);
	}

	static void playOrPause(AppContext& ctx, std::size_t last)
	{
		// From the end, with no loop to go round, Play starts again from the first frame.
		if (!ctx.playback.playing() && !ctx.playback.loops() && ctx.frame >= last)
			ctx.goToFrame(0);
		ctx.playback.toggle();
	}

	// The step between the ruler's numbered frames: 1, 2 or 5 times a power of ten, the smallest that keeps
	// numbers `apart` points apart.
	static std::size_t labelStep(double slot, double apart)
	{
		for (std::size_t power = 1;; power *= 10)
		{
			for (const std::size_t step : {power, 2 * power, 5 * power})
			{
				if (double(step) * slot >= apart || step >= (std::size_t(1) << 40))
					return step;
			}
		}
	}

	static void transport(AppContext& ctx, std::size_t last, double rate)
	{
		const rp::RigPlan& plan = ctx.document.plan;
		if (gui::Button(ctx.playback.playing() ? "Pause" : "Play"))
			playOrPause(ctx, last);
		gui::SetItemTooltip("Space");
		gui::SameLine();
		if (gui::Button("|<"))
			goTo(ctx, 0);
		gui::SetItemTooltip("The first frame");
		gui::SameLine();
		if (gui::Button("<"))
			goTo(ctx, ctx.frame > last ? last : (ctx.frame > 0 ? ctx.frame - 1 : 0));
		gui::SetItemTooltip("The previous frame");
		gui::SameLine();
		if (gui::Button(">"))
			goTo(ctx, std::min(ctx.frame + 1, last));
		gui::SetItemTooltip("The next frame");
		gui::SameLine();
		if (gui::Button(">|"))
			goTo(ctx, last);
		gui::SetItemTooltip("The last frame");
		gui::SameLine();
		bool loop = ctx.playback.loops();
		if (gui::Checkbox("Loop", &loop))
			ctx.playback.setLoop(loop);
		gui::SameLine();
		gui::BeginDisabled(!canKey(plan, ctx.selection));
		if (gui::Button("Key (K)"))
			ctx.apply(setKey(ctx.document.plan, ctx.selection, ctx.frame));
		gui::EndDisabled();
		gui::SetItemTooltip("Keys the selected mover at this frame, where its path already puts it");
		gui::SameLine();
		const std::string readout = plan.timeline.rate.specified()
										? string::format("frame {} of 0 to {}, at {}", ctx.frame, last, plan.timeline.rate.toString())
										: string::format("frame {} of 0 to {}, at no stated rate (played at {:g} fps)", ctx.frame, last, rate);
		gui::TextDisabled("%s", readout.c_str());
	}

	// The diamond a key is drawn as, centred on `at`.
	static void diamond(ImDrawList* draw, ImVec2 at, float radius, ImU32 fill, bool outlined)
	{
		const ImVec2 top{at.x, at.y - radius};
		const ImVec2 right{at.x + radius, at.y};
		const ImVec2 bottom{at.x, at.y + radius};
		const ImVec2 left{at.x - radius, at.y};
		draw->AddQuadFilled(top, right, bottom, left, fill);
		if (outlined)
			draw->AddQuad(top, right, bottom, left, gui::GetColorU32(ImGuiCol_Text), 1.5f);
	}

	void TimelinePane::draw(AppContext& ctx)
	{
		const rp::RigPlan& plan = ctx.document.plan;
		const std::size_t frames = std::max<std::size_t>(1, plan.timeline.frames);
		const std::size_t last = frames - 1;
		const double rate = plan.timeline.rate.specified() ? plan.timeline.rate.hz() : kUnspecifiedRateHz;
		const ImGuiIO& io = gui::GetIO();

		// The clock, before the pane is drawn and whether or not it is: a tab behind another still plays.
		if (ctx.playback.playing())
		{
			const std::size_t to = ctx.playback.advance(ctx.frame, last, rate, double(io.DeltaTime));
			if (to != ctx.frame)
				ctx.goToFrame(to);
		}
		if (gui::keysAreFree() && gui::IsKeyPressed(ImGuiKey_Space, false))
			playOrPause(ctx, last);

		if (gui::Begin("Timeline"))
		{
			transport(ctx, last, rate);

			const std::vector<MoverRow> rows = moverRows(plan);
			const ImGuiStyle& style = gui::GetStyle();
			const ImVec2 origin = gui::GetCursorScreenPos();
			const float available = std::max(1.0f, gui::GetContentRegionAvail().x);
			float nameWidth = gui::CalcTextSize("frame").x;
			for (const MoverRow& row : rows)
				nameWidth = std::max(nameWidth, gui::CalcTextSize(row.name.c_str()).x);
			nameWidth = std::min(nameWidth + style.ItemSpacing.x * 2.0f, available * 0.4f);

			TimelineScale scale;
			scale.left = double(origin.x + nameWidth);
			scale.width = std::max(1.0, double(available - nameWidth));
			scale.frames = frames;
			scale.overflow = keysBeyond(plan) ? std::min(kOverflowWidth, scale.width * 0.5) : 0.0;
			const double radius = std::max(kTickRadius, scale.slot() * 0.5);
			const float rowHeight = gui::GetFrameHeight();
			const float trackWidth = float(scale.width);
			ImDrawList* draw = gui::GetWindowDrawList();
			const ImU32 textColour = gui::GetColorU32(ImGuiCol_Text);
			const ImU32 gridColour = gui::GetColorU32(ImGuiCol_Border);
			const ImU32 beyondColour = gui::packColor(kBeyondColour);
			const ImU32 beyondBackground = gui::packColor({kBeyondColour.r, kBeyondColour.g, kBeyondColour.b, 40});
			const float stripLeft = float(scale.left + scale.width - scale.overflow);

			// The ruler, which is the scrubber: held, it goes to the frame under the cursor.
			float y = origin.y;
			gui::SetCursorScreenPos({origin.x, y});
			gui::AlignTextToFramePadding();
			gui::TextDisabled("frame");
			gui::SetCursorScreenPos({float(scale.left), y});
			gui::InvisibleButton("ruler", {trackWidth, rowHeight});
			if (gui::IsItemActive())
				goTo(ctx, scale.frameAt(double(io.MousePos.x)));
			if (gui::IsItemHovered())
			{
				if (scale.overflow > 0.0 && io.MousePos.x >= stripLeft)
					gui::SetTooltip("Keys past the timeline's last frame, %zu: a capture never reaches them", last);
				else
					gui::SetTooltip("frame %zu", scale.frameAt(double(io.MousePos.x)));
			}
			draw->AddRectFilled({float(scale.left), y}, {float(scale.left) + trackWidth, y + rowHeight}, gui::GetColorU32(ImGuiCol_FrameBg));
			const std::size_t step = labelStep(scale.slot(), gui::CalcTextSize("00000").x);
			for (std::size_t f = 0; f < frames; f += step)
			{
				const float x = float(scale.xOf(f));
				draw->AddLine({x, y + rowHeight * 0.6f}, {x, y + rowHeight}, gridColour);
				draw->AddText({x + 2.0f, y + style.FramePadding.y * 0.5f}, textColour, std::to_string(f).c_str());
			}
			y += rowHeight;

			// A row a mover, with its keys as ticks.
			for (const MoverRow& row : rows)
			{
				gui::PushID(row.mover.id.toString().c_str());
				const bool moverSelected = ctx.selection.id == row.mover.id &&
										   (ctx.selection.kind == row.mover.kind || ctx.selection.kind == Selected::Key);
				gui::SetCursorScreenPos({origin.x, y});
				if (gui::Selectable((row.name + "##name").c_str(), moverSelected, 0, {std::max(1.0f, nameWidth - style.ItemSpacing.x), rowHeight}))
					ctx.selection = row.mover;

				gui::SetCursorScreenPos({float(scale.left), y});
				gui::InvisibleButton("track", {trackWidth, rowHeight});
				const double mouse = double(io.MousePos.x);
				// Set in a branch, not through a conditional's temporary: GCC 13 at -O3 calls the empty
				// optional's payload maybe uninitialized where a key is selected, though that arm never
				// reads it (CI, 2026-11-03).
				std::optional<std::size_t> under;
				if (gui::IsItemHovered())
					under = nearestKey(scale, *row.path, mouse, radius);
				if (gui::IsItemClicked(ImGuiMouseButton_Left))
				{
					if (under)
					{
						// A key selected is the frame shown, so the clock stops rather than move off it.
						ctx.playback.pause();
						ctx.selection = Selection::keyOf(row.mover.id, *under);
					}
					else
					{
						ctx.selection = row.mover;
						goTo(ctx, scale.frameAt(mouse));
					}
				}
				if (under)
				{
					const std::size_t frame = row.path->keys[*under].frame;
					if (scale.beyond(frame))
						gui::SetTooltip("key %zu at frame %zu, past the timeline's last frame, %zu: a capture never reaches it", *under, frame, last);
					else
						gui::SetTooltip("key %zu at frame %zu", *under, frame);
				}

				draw->AddLine({float(scale.left), y + rowHeight - 0.5f}, {float(scale.left) + trackWidth, y + rowHeight - 0.5f}, gridColour);
				if (scale.overflow > 0.0)
					draw->AddRectFilled({stripLeft, y}, {float(scale.left) + trackWidth, y + rowHeight}, beyondBackground);
				const float tick = std::min(rowHeight * 0.3f, 6.0f);
				for (std::size_t k = 0; k < row.path->keys.size(); ++k)
				{
					const std::size_t frame = row.path->keys[k].frame;
					const bool selected = ctx.selection == Selection::keyOf(row.mover.id, k);
					const ImU32 fill = selected ? gui::GetColorU32(ImGuiCol_CheckMark) : scale.beyond(frame) ? beyondColour
																											 : textColour;
					diamond(draw, {float(scale.xOf(frame)), y + rowHeight * 0.5f}, selected ? tick * 1.3f : tick, fill, selected);
				}
				y += rowHeight;
				gui::PopID();
			}

			if (rows.empty())
			{
				gui::SetCursorScreenPos({origin.x, y});
				gui::TextDisabled("Nothing moves: a board, or a prop ticked Moves along a path, has its keys here.");
				y = gui::GetCursorScreenPos().y;
			}

			// The frame shown, across the ruler and every row.
			const float playhead = float(scale.xOf(ctx.frame));
			const ImU32 playheadColour = gui::GetColorU32(ImGuiCol_PlotLinesHovered);
			draw->AddLine({playhead, origin.y}, {playhead, y}, playheadColour, 2.0f);
			draw->AddTriangleFilled({playhead - 5.0f, origin.y}, {playhead + 5.0f, origin.y}, {playhead, origin.y + 6.0f}, playheadColour);

			// The content's extent, as an item, so the window scrolls to the last row.
			gui::SetCursorScreenPos({origin.x, y});
			gui::Dummy({1.0f, 1.0f});
		}
		gui::End();
	}
} // namespace rigplan
