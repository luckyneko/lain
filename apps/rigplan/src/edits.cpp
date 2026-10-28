#include "edits.h"

#include <lain/core/length.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

namespace rigplan
{
	using lain::core::Length;
	using lain::core::Uuid;
	using lain::string::format;
	namespace rp = lain::rigplan;

	// ASCII lower case, as check() folds a camera's name: what a case-insensitive file system folds.
	static std::string folded(std::string_view name)
	{
		std::string out(name);
		for (char& c : out)
			c = char(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	template <typename T>
	static std::vector<std::string> namesOf(const std::vector<T>& items)
	{
		std::vector<std::string> names;
		for (const T& item : items)
			names.push_back(item.name);
		return names;
	}

	static bool taken(const std::vector<std::string>& names, std::string_view name)
	{
		const std::string key = folded(name);
		return std::any_of(names.begin(), names.end(), [&](const std::string& n)
						   { return folded(n) == key; });
	}

	// `base` with a number after it that no name in `names` has, ignoring case: "cam1 2", then "cam1 3".
	// A number `base` already ends in is replaced rather than stacked, so a copy of "cam1 2" is "cam1 3".
	static std::string numbered(std::string base, const std::vector<std::string>& names)
	{
		const std::size_t space = base.find_last_of(' ');
		if (space != std::string::npos && space + 1 < base.size() &&
			std::all_of(base.begin() + std::ptrdiff_t(space) + 1, base.end(), [](char c)
						{ return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
			base.erase(space);
		for (int n = 2;; ++n)
		{
			std::string candidate = format("{} {}", base, n);
			if (!taken(names, candidate))
				return candidate;
		}
	}

	// `base` itself when no name in `names` has it, else numbered().
	static std::string unique(const std::string& base, const std::vector<std::string>& names)
	{
		return taken(names, base) ? numbered(base, names) : base;
	}

	template <typename T>
	static T* byId(std::vector<T>& items, const Uuid& id)
	{
		const auto found = std::find_if(items.begin(), items.end(), [&](const T& item)
										{ return item.id == id; });
		return found != items.end() ? &*found : nullptr;
	}

	template <typename T>
	static T* byName(std::vector<T>& items, const std::string& name)
	{
		const auto found = std::find_if(items.begin(), items.end(), [&](const T& item)
										{ return item.name == name; });
		return found != items.end() ? &*found : nullptr;
	}

	template <typename T>
	static bool eraseId(std::vector<T>& items, const Uuid& id)
	{
		const auto end = std::remove_if(items.begin(), items.end(), [&](const T& item)
										{ return item.id == id; });
		const bool found = end != items.end();
		items.erase(end, items.end());
		return found;
	}

	// Where something new stands: the middle of the room's floor, or the plan frame's origin.
	static lain::math::Vec2d centre(const rp::RigPlan& plan)
	{
		return plan.room ? plan.room->centre : lain::math::Vec2d{0.0, 0.0};
	}

	EditResult addPole(rp::RigPlan& plan)
	{
		rp::Mount mount;
		mount.id = Uuid::generate();
		mount.name = unique("pole", namesOf(plan.mounts));
		mount.kind = rp::Pole{centre(plan), 2.5, 0.05};
		plan.mounts.push_back(mount);
		return EditResult::done(Selection::object(Selected::Mount, mount.id));
	}

	EditResult addFixedPoint(rp::RigPlan& plan)
	{
		const lain::math::Vec2d floor = centre(plan);
		rp::Mount mount;
		mount.id = Uuid::generate();
		mount.name = unique("fixed point", namesOf(plan.mounts));
		mount.kind = rp::FixedPoint{{floor.x, floor.y, 2.0}};
		plan.mounts.push_back(mount);
		return EditResult::done(Selection::object(Selected::Mount, mount.id));
	}

	// The mount `on` names: a mount itself, or the mount a camera or light is on.
	static const rp::Mount* mountFor(rp::RigPlan& plan, const Selection& on)
	{
		Uuid id;
		if (on.kind == Selected::Mount)
			id = on.id;
		else if (on.kind == Selected::Camera)
		{
			if (const rp::PlacedCamera* camera = byId(plan.cameras, on.id))
				id = camera->mount;
		}
		else if (on.kind == Selected::Light)
		{
			if (const rp::PlacedLight* light = byId(plan.lights, on.id))
				id = light->mount;
		}
		return id.isNil() ? nullptr : byId(plan.mounts, id);
	}

	// Where a new camera or light goes on `mount`: out from a pole's axis, so it is clear of the pole, at
	// a camera's height; at a fixed point itself.
	static lain::math::Vec3d placement(const rp::Mount& mount)
	{
		return std::holds_alternative<rp::Pole>(mount.kind) ? lain::math::Vec3d{0.0, 0.125, 1.5} : lain::math::Vec3d{0.0, 0.0, 0.0};
	}

	EditResult addCamera(rp::RigPlan& plan, const Selection& on)
	{
		const rp::Mount* mount = mountFor(plan, on);
		if (!mount)
			return EditResult::refused("select a mount to put the camera on");
		if (plan.specifications.cameras.empty())
			return EditResult::refused("the plan has no camera specification to place");
		rp::PlacedCamera camera;
		camera.id = Uuid::generate();
		const std::vector<std::string> names = namesOf(plan.cameras);
		for (std::size_t n = 1;; ++n)
		{
			camera.name = format("cam{}", n);
			if (!taken(names, camera.name))
				break;
		}
		camera.camera = plan.specifications.cameras.front().name;
		camera.mount = mount->id;
		camera.offset = placement(*mount);
		plan.cameras.push_back(camera);
		return EditResult::done(Selection::object(Selected::Camera, camera.id));
	}

	EditResult addLight(rp::RigPlan& plan, const Selection& on)
	{
		const rp::Mount* mount = mountFor(plan, on);
		if (!mount)
			return EditResult::refused("select a mount to put the light on");
		if (plan.specifications.lights.empty())
			return EditResult::refused("the plan has no light specification to place");
		rp::PlacedLight light;
		light.id = Uuid::generate();
		light.name = unique("light", namesOf(plan.lights));
		light.light = plan.specifications.lights.front().name;
		light.mount = mount->id;
		light.offset = placement(*mount);
		plan.lights.push_back(light);
		return EditResult::done(Selection::object(Selected::Light, light.id));
	}

	EditResult addPlane(rp::RigPlan& plan)
	{
		const lain::math::Vec2d floor = centre(plan);
		rp::Plane plane;
		plane.id = Uuid::generate();
		plane.name = unique("plane", namesOf(plan.planes));
		plane.centre = {floor.x, floor.y, 1.0};
		plan.planes.push_back(plane);
		return EditResult::done(Selection::object(Selected::Plane, plane.id));
	}

	EditResult addBox(rp::RigPlan& plan)
	{
		const lain::math::Vec2d floor = centre(plan);
		rp::Box box;
		box.id = Uuid::generate();
		box.name = unique("box", namesOf(plan.boxes));
		box.centre = {floor.x, floor.y, 0.5};
		plan.boxes.push_back(box);
		return EditResult::done(Selection::object(Selected::Box, box.id));
	}

	EditResult addProp(rp::RigPlan& plan, lain::core::Uri mesh)
	{
		const lain::math::Vec2d floor = centre(plan);
		rp::Prop prop;
		prop.id = Uuid::generate();
		prop.name = unique("prop", namesOf(plan.props));
		prop.mesh = std::move(mesh);
		prop.position = {floor.x, floor.y, 0.0};
		plan.props.push_back(prop);
		return EditResult::done(Selection::object(Selected::Prop, prop.id));
	}

	EditResult addBoard(rp::RigPlan& plan)
	{
		const lain::math::Vec2d floor = centre(plan);
		rp::BoardMover board;
		board.id = Uuid::generate();
		board.name = unique("board", namesOf(plan.boards));
		board.pattern.dictionary = lain::camera::board::Dictionary::Aruco5x5_100;
		board.pattern.squaresX = 7;
		board.pattern.squaresY = 5;
		board.pattern.markerToSquare = 0.75;
		std::vector<std::string> identities;
		for (const rp::BoardMover& b : plan.boards)
			identities.push_back(b.instance.identity);
		board.instance.identity = unique("board", identities);
		board.instance.squareLength.value = Length::from<Length::Millimetres>(24.0);
		board.path.keys = {{0, {floor.x, floor.y, 1.25}, {}}};
		plan.boards.push_back(board);
		return EditResult::done(Selection::object(Selected::Board, board.id));
	}

	EditResult addRoom(rp::RigPlan& plan)
	{
		if (plan.room)
			return EditResult::refused("the plan has a room already");
		plan.room = rp::Room{};
		return EditResult::done(Selection::room());
	}

	// What names the specification `selection` names, for a person, or empty when nothing does.
	static std::string userOf(const rp::RigPlan& plan, const Selection& selection)
	{
		const std::string& name = selection.name;
		switch (selection.specification)
		{
			case SpecificationKind::Sensor:
			case SpecificationKind::Lens:
				for (const rp::CameraSpecification& c : plan.specifications.cameras)
				{
					const std::string& part = selection.specification == SpecificationKind::Sensor ? c.sensor : c.lens;
					if (part == name)
						return format("camera specification '{}'", c.name);
				}
				return {};
			case SpecificationKind::Camera:
				for (const rp::PlacedCamera& c : plan.cameras)
				{
					if (c.camera == name)
						return format("camera '{}'", c.name);
				}
				return {};
			case SpecificationKind::Light:
				for (const rp::PlacedLight& l : plan.lights)
				{
					if (l.light == name)
						return format("light '{}'", l.name);
				}
				return {};
		}
		return {};
	}

	static const char* kindName(SpecificationKind kind)
	{
		switch (kind)
		{
			case SpecificationKind::Sensor:
				return "sensor";
			case SpecificationKind::Lens:
				return "lens";
			case SpecificationKind::Camera:
				return "camera specification";
			case SpecificationKind::Light:
				return "light specification";
		}
		return "specification";
	}

	template <typename T>
	static bool eraseName(std::vector<T>& items, const std::string& name)
	{
		const auto end = std::remove_if(items.begin(), items.end(), [&](const T& item)
										{ return item.name == name; });
		const bool found = end != items.end();
		items.erase(end, items.end());
		return found;
	}

	static bool eraseSpecification(rp::Specifications& specifications, SpecificationKind kind, const std::string& name)
	{
		switch (kind)
		{
			case SpecificationKind::Sensor:
				return eraseName(specifications.sensors, name);
			case SpecificationKind::Lens:
				return eraseName(specifications.lenses, name);
			case SpecificationKind::Camera:
				return eraseName(specifications.cameras, name);
			case SpecificationKind::Light:
				return eraseName(specifications.lights, name);
		}
		return false;
	}

	// A camera deleted: the reference goes with it, rather than naming a camera the plan lacks.
	static void forgetReference(rp::RigPlan& plan, const Uuid& camera)
	{
		if (plan.reference && *plan.reference == camera)
			plan.reference.reset();
	}

	EditResult remove(rp::RigPlan& plan, const Selection& selection)
	{
		const Uuid& id = selection.id;
		bool found = false;
		switch (selection.kind)
		{
			case Selected::Nothing:
			case Selected::Plan:
				return EditResult::nothing();
			case Selected::Room:
				found = plan.room.has_value();
				plan.room.reset();
				break;
			case Selected::Mount:
				found = eraseId(plan.mounts, id);
				if (found)
				{
					for (const rp::PlacedCamera& camera : plan.cameras)
					{
						if (camera.mount == id)
							forgetReference(plan, camera.id);
					}
					plan.cameras.erase(std::remove_if(plan.cameras.begin(), plan.cameras.end(), [&](const rp::PlacedCamera& c)
													  { return c.mount == id; }),
									   plan.cameras.end());
					plan.lights.erase(std::remove_if(plan.lights.begin(), plan.lights.end(), [&](const rp::PlacedLight& l)
													 { return l.mount == id; }),
									  plan.lights.end());
				}
				break;
			case Selected::Camera:
				found = eraseId(plan.cameras, id);
				forgetReference(plan, id);
				break;
			case Selected::Light:
				found = eraseId(plan.lights, id);
				break;
			case Selected::Plane:
				found = eraseId(plan.planes, id);
				break;
			case Selected::Box:
				found = eraseId(plan.boxes, id);
				break;
			case Selected::Prop:
				found = eraseId(plan.props, id);
				break;
			case Selected::Board:
				found = eraseId(plan.boards, id);
				break;
			case Selected::Key:
			{
				rp::Path* path = pathOf(plan, id);
				if (!path || selection.key >= path->keys.size())
					return EditResult::nothing();
				if (path->keys.size() == 1)
					return EditResult::refused("a path needs a key: delete the mover instead of its last key");
				path->keys.erase(path->keys.begin() + std::ptrdiff_t(selection.key));
				return EditResult::done(Selection::nothing());
			}
			case Selected::Specification:
			{
				const std::string user = userOf(plan, selection);
				if (!user.empty())
					return EditResult::refused(format("{} '{}' is used by {}", kindName(selection.specification), selection.name, user));
				found = eraseSpecification(plan.specifications, selection.specification, selection.name);
				break;
			}
		}
		return found ? EditResult::done(Selection::nothing()) : EditResult::nothing();
	}

	// A copy of what `items` holds under `id`, under a new identity and name, appended to `items`.
	template <typename T>
	static T* copied(std::vector<T>& items, const Uuid& id)
	{
		const T* original = byId(items, id);
		if (!original)
			return nullptr;
		T copy = *original;
		copy.id = Uuid::generate();
		copy.name = numbered(copy.name, namesOf(items));
		items.push_back(std::move(copy)); // `original` may dangle from here
		return &items.back();
	}

	// A copy of the specification `name` names in `items`, under a name its kind does not have.
	template <typename T>
	static bool copiedSpecification(std::vector<T>& items, const std::string& name, std::string& copyName)
	{
		const T* original = byName(items, name);
		if (!original)
			return false;
		T copy = *original;
		copy.name = numbered(copy.name, namesOf(items));
		copyName = copy.name;
		items.push_back(std::move(copy));
		return true;
	}

	EditResult duplicate(rp::RigPlan& plan, const Selection& selection)
	{
		const Uuid& id = selection.id;
		switch (selection.kind)
		{
			case Selected::Nothing:
				return EditResult::nothing();
			case Selected::Plan:
				return EditResult::refused("a plan is not copied into itself: save it under another name");
			case Selected::Room:
				return EditResult::refused("a plan has one room");
			case Selected::Mount:
			{
				const rp::Mount* mount = copied(plan.mounts, id);
				if (!mount)
					return EditResult::nothing();
				const Uuid copy = mount->id;
				// What the original carries, carried by the copy: each a new unit, of a new name.
				const std::size_t cameras = plan.cameras.size();
				for (std::size_t i = 0; i < cameras; ++i)
				{
					if (plan.cameras[i].mount != id)
						continue;
					rp::PlacedCamera camera = plan.cameras[i];
					camera.id = Uuid::generate();
					camera.name = numbered(camera.name, namesOf(plan.cameras));
					camera.mount = copy;
					plan.cameras.push_back(std::move(camera));
				}
				const std::size_t lights = plan.lights.size();
				for (std::size_t i = 0; i < lights; ++i)
				{
					if (plan.lights[i].mount != id)
						continue;
					rp::PlacedLight light = plan.lights[i];
					light.id = Uuid::generate();
					light.name = numbered(light.name, namesOf(plan.lights));
					light.mount = copy;
					plan.lights.push_back(std::move(light));
				}
				return EditResult::done(Selection::object(Selected::Mount, copy));
			}
			case Selected::Camera:
			{
				const rp::PlacedCamera* camera = copied(plan.cameras, id);
				return camera ? EditResult::done(Selection::object(Selected::Camera, camera->id)) : EditResult::nothing();
			}
			case Selected::Light:
			{
				const rp::PlacedLight* light = copied(plan.lights, id);
				return light ? EditResult::done(Selection::object(Selected::Light, light->id)) : EditResult::nothing();
			}
			case Selected::Plane:
			{
				const rp::Plane* plane = copied(plan.planes, id);
				return plane ? EditResult::done(Selection::object(Selected::Plane, plane->id)) : EditResult::nothing();
			}
			case Selected::Box:
			{
				const rp::Box* box = copied(plan.boxes, id);
				return box ? EditResult::done(Selection::object(Selected::Box, box->id)) : EditResult::nothing();
			}
			case Selected::Prop:
			{
				const rp::Prop* prop = copied(plan.props, id);
				return prop ? EditResult::done(Selection::object(Selected::Prop, prop->id)) : EditResult::nothing();
			}
			case Selected::Board:
			{
				std::vector<std::string> identities;
				for (const rp::BoardMover& b : plan.boards)
					identities.push_back(b.instance.identity);
				rp::BoardMover* board = copied(plan.boards, id);
				if (!board)
					return EditResult::nothing();
				board->instance.identity = numbered(board->instance.identity, identities);
				return EditResult::done(Selection::object(Selected::Board, board->id));
			}
			case Selected::Key:
			{
				rp::Path* path = pathOf(plan, id);
				if (!path || selection.key >= path->keys.size())
					return EditResult::nothing();
				rp::PathKey copy = path->keys[selection.key];
				++copy.frame;
				const std::size_t next = selection.key + 1;
				if (next < path->keys.size() && path->keys[next].frame <= copy.frame)
					return EditResult::refused(format("frame {} has a key already", copy.frame));
				path->keys.insert(path->keys.begin() + std::ptrdiff_t(next), copy);
				return EditResult::done(Selection::keyOf(id, next));
			}
			case Selected::Specification:
			{
				std::string name;
				rp::Specifications& s = plan.specifications;
				bool found = false;
				switch (selection.specification)
				{
					case SpecificationKind::Sensor:
						found = copiedSpecification(s.sensors, selection.name, name);
						break;
					case SpecificationKind::Lens:
						found = copiedSpecification(s.lenses, selection.name, name);
						break;
					case SpecificationKind::Camera:
						found = copiedSpecification(s.cameras, selection.name, name);
						break;
					case SpecificationKind::Light:
						found = copiedSpecification(s.lights, selection.name, name);
						break;
				}
				return found ? EditResult::done(Selection::specificationOf(selection.specification, name)) : EditResult::nothing();
			}
		}
		return EditResult::nothing();
	}

	// Renames the specification `from` names in `items` to `to`, and every name in `references` that was
	// `from`. Refused when another of its kind is `to` already.
	template <typename T>
	static EditResult renamed(std::vector<T>& items, const Selection& selection, const std::string& to, const std::vector<std::string*>& references)
	{
		T* specification = byName(items, selection.name);
		if (!specification)
			return EditResult::nothing();
		if (byName(items, to))
			return EditResult::refused(format("another {} is named '{}'", kindName(selection.specification), to));
		specification->name = to;
		for (std::string* reference : references)
		{
			if (*reference == selection.name)
				*reference = to;
		}
		return EditResult::done(Selection::specificationOf(selection.specification, to));
	}

	// Gives the object `id` names in `items` the name `name`.
	template <typename T>
	static EditResult named(std::vector<T>& items, const Selection& selection, const std::string& name)
	{
		T* item = byId(items, selection.id);
		if (!item)
			return EditResult::nothing();
		item->name = name;
		return EditResult::done(selection);
	}

	// The name `selection` names now, or none when it has no name.
	static std::optional<std::string> currentName(rp::RigPlan& plan, const Selection& selection)
	{
		const auto nameIn = [&](auto& items) -> std::optional<std::string>
		{
			if (const auto* item = byId(items, selection.id))
				return item->name;
			return std::nullopt;
		};
		switch (selection.kind)
		{
			case Selected::Mount:
				return nameIn(plan.mounts);
			case Selected::Camera:
				return nameIn(plan.cameras);
			case Selected::Light:
				return nameIn(plan.lights);
			case Selected::Plane:
				return nameIn(plan.planes);
			case Selected::Box:
				return nameIn(plan.boxes);
			case Selected::Prop:
				return nameIn(plan.props);
			case Selected::Board:
				return nameIn(plan.boards);
			case Selected::Specification:
				return selection.name;
			case Selected::Nothing:
			case Selected::Plan:
			case Selected::Room:
			case Selected::Key:
				return std::nullopt;
		}
		return std::nullopt;
	}

	EditResult rename(rp::RigPlan& plan, const Selection& selection, const std::string& name)
	{
		const std::optional<std::string> current = currentName(plan, selection);
		if (!current)
			return EditResult::nothing();
		if (name == *current)
			return EditResult::nothing();
		if (name.empty())
			return EditResult::refused("a name cannot be empty");

		rp::Specifications& s = plan.specifications;
		switch (selection.kind)
		{
			case Selected::Camera:
			{
				if (!rp::validFileName(name))
					return EditResult::refused(format("'{}' cannot be a camera's name: it is the folder its footage is written to, and is not a "
													  "valid folder name on every platform",
													  name));
				for (const rp::PlacedCamera& camera : plan.cameras)
				{
					if (camera.id != selection.id && folded(camera.name) == folded(name))
						return EditResult::refused(format("camera '{}' has that name already, ignoring case", camera.name));
				}
				return named(plan.cameras, selection, name);
			}
			case Selected::Board:
				for (const rp::BoardMover& board : plan.boards)
				{
					if (board.id != selection.id && board.name == name)
						return EditResult::refused(format("another board is named '{}'", name));
				}
				return named(plan.boards, selection, name);
			case Selected::Mount:
				return named(plan.mounts, selection, name);
			case Selected::Light:
				return named(plan.lights, selection, name);
			case Selected::Plane:
				return named(plan.planes, selection, name);
			case Selected::Box:
				return named(plan.boxes, selection, name);
			case Selected::Prop:
				return named(plan.props, selection, name);
			case Selected::Specification:
			{
				std::vector<std::string*> references;
				switch (selection.specification)
				{
					case SpecificationKind::Sensor:
						for (rp::CameraSpecification& c : s.cameras)
							references.push_back(&c.sensor);
						return renamed(s.sensors, selection, name, references);
					case SpecificationKind::Lens:
						for (rp::CameraSpecification& c : s.cameras)
							references.push_back(&c.lens);
						return renamed(s.lenses, selection, name, references);
					case SpecificationKind::Camera:
						for (rp::PlacedCamera& c : plan.cameras)
							references.push_back(&c.camera);
						return renamed(s.cameras, selection, name, references);
					case SpecificationKind::Light:
						for (rp::PlacedLight& l : plan.lights)
							references.push_back(&l.light);
						return renamed(s.lights, selection, name, references);
				}
				return EditResult::nothing();
			}
			case Selected::Nothing:
			case Selected::Plan:
			case Selected::Room:
			case Selected::Key:
				return EditResult::nothing();
		}
		return EditResult::nothing();
	}

	EditResult setReference(rp::RigPlan& plan, const Selection& camera)
	{
		if (camera.kind == Selected::Nothing)
		{
			if (!plan.reference)
				return EditResult::nothing();
			plan.reference.reset();
			return EditResult::done(Selection::plan());
		}
		if (camera.kind != Selected::Camera || !byId(plan.cameras, camera.id))
			return EditResult::refused("only a camera of the plan can be its reference");
		if (plan.reference && *plan.reference == camera.id)
			return EditResult::nothing();
		plan.reference = camera.id;
		return EditResult::done(camera);
	}
} // namespace rigplan
