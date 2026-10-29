#include "selection.h"

#include <algorithm>
#include <utility>

namespace rigplan
{
	using lain::core::Uuid;
	namespace rp = lain::rigplan;

	template <typename T>
	static bool hasId(const std::vector<T>& items, const Uuid& id)
	{
		return std::any_of(items.begin(), items.end(), [&](const T& item)
						   { return item.id == id; });
	}

	template <typename T>
	static bool hasName(const std::vector<T>& items, const std::string& name)
	{
		return std::any_of(items.begin(), items.end(), [&](const T& item)
						   { return item.name == name; });
	}

	const rp::Path* pathOf(const rp::RigPlan& plan, const Uuid& id)
	{
		for (const rp::BoardMover& board : plan.boards)
		{
			if (board.id == id)
				return &board.path;
		}
		for (const rp::Prop& prop : plan.props)
		{
			if (prop.id == id)
				return prop.path ? &*prop.path : nullptr;
		}
		return nullptr;
	}

	rp::Path* pathOf(rp::RigPlan& plan, const Uuid& id)
	{
		return const_cast<rp::Path*>(pathOf(static_cast<const rp::RigPlan&>(plan), id));
	}

	bool exists(const rp::RigPlan& plan, const Selection& selection)
	{
		switch (selection.kind)
		{
			case Selected::Nothing:
			case Selected::Plan:
				return true;
			case Selected::Room:
				return plan.room.has_value();
			case Selected::Mount:
				return hasId(plan.mounts, selection.id);
			case Selected::Camera:
				return hasId(plan.cameras, selection.id);
			case Selected::Light:
				return hasId(plan.lights, selection.id);
			case Selected::Plane:
				return hasId(plan.planes, selection.id);
			case Selected::Box:
				return hasId(plan.boxes, selection.id);
			case Selected::Prop:
				return hasId(plan.props, selection.id);
			case Selected::Board:
				return hasId(plan.boards, selection.id);
			case Selected::Key:
			{
				const rp::Path* path = pathOf(plan, selection.id);
				return path && selection.key < path->keys.size();
			}
			case Selected::Specification:
				switch (selection.specification)
				{
					case SpecificationKind::Sensor:
						return hasName(plan.specifications.sensors, selection.name);
					case SpecificationKind::Lens:
						return hasName(plan.specifications.lenses, selection.name);
					case SpecificationKind::Camera:
						return hasName(plan.specifications.cameras, selection.name);
					case SpecificationKind::Light:
						return hasName(plan.specifications.lights, selection.name);
				}
				return false;
		}
		return false;
	}

	Selection selectionOf(const rp::RigPlan& plan, const Uuid& owner)
	{
		if (owner.isNil())
			return Selection::nothing();
		const std::pair<Selected, bool> kinds[] = {
			{Selected::Mount, hasId(plan.mounts, owner)},
			{Selected::Camera, hasId(plan.cameras, owner)},
			{Selected::Light, hasId(plan.lights, owner)},
			{Selected::Plane, hasId(plan.planes, owner)},
			{Selected::Box, hasId(plan.boxes, owner)},
			{Selected::Prop, hasId(plan.props, owner)},
			{Selected::Board, hasId(plan.boards, owner)},
		};
		for (const auto& [kind, found] : kinds)
		{
			if (found)
				return Selection::object(kind, owner);
		}
		return Selection::nothing();
	}

	std::size_t shownFrame(const rp::RigPlan& plan, const Selection& selection)
	{
		if (selection.kind != Selected::Key)
			return 0;
		const rp::Path* path = pathOf(plan, selection.id);
		return path && selection.key < path->keys.size() ? path->keys[selection.key].frame : 0;
	}
} // namespace rigplan
