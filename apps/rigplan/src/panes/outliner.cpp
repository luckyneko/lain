#include "outliner.h"

#include "../appcontext.h"

#include <lain/gui/color.h>
#include <lain/gui/gui.h>
#include <lain/rigplan/rigplan.h>
#include <lain/string/format.h>

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	static const image::ColorRGBA8 kProblemColour{230, 180, 60, 255};

	// One frame's tree: what is selected, and what check() says about whom.
	struct OutlinerTree
	{
		AppContext& ctx;
		std::map<core::Uuid, std::vector<std::string>> problems;

		// A row for `selection`: a leaf, or a node whose children follow while it returns true (and then
		// TreePop is the caller's). A click that does not open or close it selects it.
		bool row(const Selection& selection, const std::string& label, bool leaf, const core::Uuid* owner = nullptr)
		{
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (leaf)
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (ctx.selection == selection)
				flags |= ImGuiTreeNodeFlags_Selected;
			const auto found = owner ? problems.find(*owner) : problems.end();
			const bool problem = found != problems.end();
			if (problem)
				gui::PushStyleColor(ImGuiCol_Text, gui::packColor(kProblemColour));
			// The identity in the id, so two rows of one name stay two rows.
			const std::string id = string::format("{}##{}{}{}{}", label, int(selection.kind), selection.id.toString(), selection.key, selection.name);
			const bool open = gui::TreeNodeEx(id.c_str(), flags);
			if (problem)
			{
				gui::PopStyleColor();
				if (gui::IsItemHovered())
				{
					std::string text;
					for (const std::string& p : found->second)
						text += (text.empty() ? "" : "\n") + p;
					gui::SetTooltip("%s", text.c_str());
				}
			}
			if (gui::IsItemClicked() && !gui::IsItemToggledOpen())
				ctx.selection = selection;
			return open && !leaf;
		}

		bool group(const char* label)
		{
			return gui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
		}

		void keys(const core::Uuid& mover, const rp::Path& path)
		{
			for (std::size_t k = 0; k < path.keys.size(); ++k)
				row(Selection::keyOf(mover, k), string::format("key at frame {}", path.keys[k].frame), true);
		}

		template <typename T>
		void specifications(const char* label, SpecificationKind kind, const std::vector<T>& items)
		{
			if (!group(label))
				return;
			for (const T& item : items)
				row(Selection::specificationOf(kind, item.name), item.name, true);
			gui::TreePop();
		}

		void placed(const rp::RigPlan& plan, const core::Uuid& mount, bool unmounted)
		{
			const auto onIt = [&](const core::Uuid& on)
			{
				if (!unmounted)
					return on == mount;
				for (const rp::Mount& m : plan.mounts)
				{
					if (m.id == on)
						return false;
				}
				return true;
			};
			for (const rp::PlacedCamera& camera : plan.cameras)
			{
				if (onIt(camera.mount))
				{
					const bool reference = plan.reference ? *plan.reference == camera.id : &camera == &plan.cameras.front();
					row(Selection::object(Selected::Camera, camera.id), "camera " + camera.name + (reference ? " (reference)" : ""), true, &camera.id);
				}
			}
			for (const rp::PlacedLight& light : plan.lights)
			{
				if (onIt(light.mount))
					row(Selection::object(Selected::Light, light.id), "light " + light.name, true, &light.id);
			}
		}
	};

	void OutlinerPane::draw(AppContext& ctx)
	{
		if (gui::Begin("Outliner"))
		{
			const rp::RigPlan& plan = ctx.document.plan;
			OutlinerTree tree{ctx, {}};
			for (const rp::PlanDiagnostic& d : rp::check(plan))
			{
				if (d.owner)
					tree.problems[*d.owner].push_back(d.detail);
			}

			tree.row(Selection::plan(), "Plan", true);
			if (plan.room)
				tree.row(Selection::room(), "Room", true);

			if (tree.group("Specifications"))
			{
				tree.specifications("Sensors", SpecificationKind::Sensor, plan.specifications.sensors);
				tree.specifications("Lenses", SpecificationKind::Lens, plan.specifications.lenses);
				tree.specifications("Cameras", SpecificationKind::Camera, plan.specifications.cameras);
				tree.specifications("Lights", SpecificationKind::Light, plan.specifications.lights);
				gui::TreePop();
			}

			if (tree.group("Mounts"))
			{
				for (const rp::Mount& mount : plan.mounts)
				{
					const char* kind = std::holds_alternative<rp::Pole>(mount.kind) ? "pole" : "fixed point";
					if (tree.row(Selection::object(Selected::Mount, mount.id), string::format("{} ({})", mount.name, kind), false, &mount.id))
					{
						tree.placed(plan, mount.id, false);
						gui::TreePop();
					}
				}
				gui::TreePop();
			}
			bool unmounted = false;
			for (const rp::PlacedCamera& c : plan.cameras)
				unmounted |= std::none_of(plan.mounts.begin(), plan.mounts.end(), [&](const rp::Mount& m)
										  { return m.id == c.mount; });
			for (const rp::PlacedLight& l : plan.lights)
				unmounted |= std::none_of(plan.mounts.begin(), plan.mounts.end(), [&](const rp::Mount& m)
										  { return m.id == l.mount; });
			if (unmounted && tree.group("Not on a mount"))
			{
				tree.placed(plan, {}, true);
				gui::TreePop();
			}

			if (!plan.planes.empty() && tree.group("Planes"))
			{
				for (const rp::Plane& plane : plan.planes)
					tree.row(Selection::object(Selected::Plane, plane.id), plane.name, true, &plane.id);
				gui::TreePop();
			}
			if (!plan.boxes.empty() && tree.group("Boxes"))
			{
				for (const rp::Box& box : plan.boxes)
					tree.row(Selection::object(Selected::Box, box.id), box.name, true, &box.id);
				gui::TreePop();
			}
			if (!plan.props.empty() && tree.group("Props"))
			{
				for (const rp::Prop& prop : plan.props)
				{
					if (tree.row(Selection::object(Selected::Prop, prop.id), prop.name + (prop.path ? " (moves)" : ""), !prop.path, &prop.id))
					{
						tree.keys(prop.id, *prop.path);
						gui::TreePop();
					}
				}
				gui::TreePop();
			}
			if (!plan.boards.empty() && tree.group("Boards"))
			{
				for (const rp::BoardMover& board : plan.boards)
				{
					if (tree.row(Selection::object(Selected::Board, board.id), board.name, false, &board.id))
					{
						tree.keys(board.id, board.path);
						gui::TreePop();
					}
				}
				gui::TreePop();
			}
		}
		gui::End();
	}
} // namespace rigplan
