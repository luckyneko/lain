#include "gizmo.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace rigplan
{
	namespace math = lain::math;
	namespace rp = lain::rigplan;
	using lain::core::Uuid;

	static constexpr GizmoAxes kAll{true, true, true};
	static constexpr GizmoAxes kFloor{true, true, false};
	static constexpr GizmoAxes kYaw{false, false, true};

	static const math::Vec3d kUp{0.0, 0.0, 1.0};

	// A pose in the plan's axes turned by `yawDegrees` about +Z: a mount's, a box's, a static prop's.
	static math::Mat4d yawed(const math::Vec3d& position, double yawDegrees)
	{
		return math::RigidTransformd{math::angleAxis(math::radians(yawDegrees), kUp), position}.matrix();
	}

	template <typename T>
	static T* byId(std::vector<T>& items, const Uuid& id)
	{
		const auto found = std::find_if(items.begin(), items.end(), [&](const T& item)
										{ return item.id == id; });
		return found != items.end() ? &*found : nullptr;
	}

	template <typename T>
	static const T* byId(const std::vector<T>& items, const Uuid& id)
	{
		const auto found = std::find_if(items.begin(), items.end(), [&](const T& item)
										{ return item.id == id; });
		return found != items.end() ? &*found : nullptr;
	}

	// Where the mount `id` stands in `layout`, or none.
	static std::optional<math::RigidTransformd> mountPose(const rp::NominalLayout& layout, const Uuid& id)
	{
		for (const rp::NominalLayout::Mount& mount : layout.mounts)
		{
			if (mount.id == id)
				return mount.planFromMount;
		}
		return std::nullopt;
	}

	std::optional<GizmoTarget> gizmoTarget(const rp::RigPlan& plan, const rp::NominalLayout& layout, const Selection& selection)
	{
		GizmoTarget target;
		switch (selection.kind)
		{
			case Selected::Mount:
			{
				const rp::Mount* mount = byId(plan.mounts, selection.id);
				if (!mount)
					return std::nullopt;
				if (const auto* pole = std::get_if<rp::Pole>(&mount->kind))
				{
					target.planFromTarget = yawed({pole->floor.x, pole->floor.y, 0.0}, mount->yawDegrees);
					target.translate = kFloor;
				}
				else
				{
					target.planFromTarget = yawed(std::get<rp::FixedPoint>(mount->kind).position, mount->yawDegrees);
					target.translate = kAll;
				}
				target.rotate = kYaw;
				return target;
			}
			case Selected::Camera:
			case Selected::Light:
			{
				// Its mounting point, where its offset puts it: not the optical centre, which is the sensor
				// body's business, so turning the aim turns it about the point it is fixed by.
				Uuid mount;
				math::Vec3d offset{0.0};
				rp::Aim aim;
				if (selection.kind == Selected::Camera)
				{
					const rp::PlacedCamera* camera = byId(plan.cameras, selection.id);
					if (!camera)
						return std::nullopt;
					mount = camera->mount;
					offset = camera->offset;
					aim = camera->aim;
				}
				else
				{
					const rp::PlacedLight* light = byId(plan.lights, selection.id);
					if (!light)
						return std::nullopt;
					mount = light->mount;
					offset = light->offset;
					aim = light->aim;
				}
				const std::optional<math::RigidTransformd> planFromMount = mountPose(layout, mount);
				if (!planFromMount)
					return std::nullopt;
				target.planFromTarget = (*planFromMount * rp::mountFromPlacement(offset, aim)).matrix();
				target.translate = kAll;
				target.rotate = kAll;
				return target;
			}
			case Selected::Plane:
			{
				const rp::Plane* plane = byId(plan.planes, selection.id);
				if (!plane)
					return std::nullopt;
				target.planFromTarget = rp::mountFromPlacement(plane->centre, plane->aim).matrix();
				target.translate = kAll;
				target.rotate = kAll;
				return target;
			}
			case Selected::Box:
			{
				const rp::Box* box = byId(plan.boxes, selection.id);
				if (!box)
					return std::nullopt;
				target.planFromTarget = yawed(box->centre, box->yawDegrees);
				target.translate = kAll;
				target.rotate = kYaw;
				return target;
			}
			case Selected::Prop:
			{
				const rp::Prop* prop = byId(plan.props, selection.id);
				if (!prop || prop->path) // a prop mover is moved by its keys
					return std::nullopt;
				target.planFromTarget = yawed(prop->position, prop->yawDegrees);
				target.translate = kAll;
				target.rotate = kYaw;
				return target;
			}
			case Selected::Key:
			{
				const rp::Path* path = pathOf(plan, selection.id);
				if (!path || selection.key >= path->keys.size())
					return std::nullopt;
				const rp::PathKey& key = path->keys[selection.key];
				target.planFromTarget = rp::mountFromPlacement(key.position, key.aim).matrix();
				target.translate = kAll;
				target.rotate = kAll;
				return target;
			}
			case Selected::Room:
				if (!plan.room)
					return std::nullopt;
				target.planFromTarget = yawed({plan.room->centre.x, plan.room->centre.y, 0.0}, 0.0);
				target.translate = kFloor;
				return target;
			case Selected::Nothing:
			case Selected::Plan:
			case Selected::Board:
			case Selected::Specification:
				return std::nullopt;
		}
		return std::nullopt;
	}

	lain::gui::gizmo::OPERATION operation(const GizmoTarget& target, GizmoMode mode)
	{
		namespace gz = lain::gui::gizmo;
		int op = 0;
		if (mode == GizmoMode::Translate)
		{
			op |= target.translate[0] ? gz::TRANSLATE_X : 0;
			op |= target.translate[1] ? gz::TRANSLATE_Y : 0;
			op |= target.translate[2] ? gz::TRANSLATE_Z : 0;
		}
		else
		{
			op |= target.rotate[0] ? gz::ROTATE_X : 0;
			op |= target.rotate[1] ? gz::ROTATE_Y : 0;
			op |= target.rotate[2] ? gz::ROTATE_Z : 0;
		}
		return static_cast<gz::OPERATION>(op);
	}

	// The rotation and translation of `m`: its axes made unit length and at right angles (Gram-Schmidt),
	// since a matrix that has been through ImGuizmo's floats carries a little scale and shear.
	static math::RigidTransformd rigidPart(const math::Mat4d& m)
	{
		const math::Vec3d x = math::normalize(math::Vec3d(m[0]));
		const math::Vec3d y = math::normalize(math::Vec3d(m[1]) - x * math::dot(x, math::Vec3d(m[1])));
		const math::Vec3d z = math::cross(x, y);
		return math::RigidTransformd{math::quat_cast(math::Mat3d(x, y, z)), math::Vec3d(m[3])};
	}

	// The yaw a turn gives its +X across the floor, degrees in (-180, 180]: all of a turn the plan can hold
	// for a kind that only yaws.
	static double yawOf(const math::Quatd& rotation)
	{
		const math::Vec3d x = rotation * math::Vec3d{1.0, 0.0, 0.0};
		return math::degrees(std::atan2(x.y, x.x));
	}

	template <typename T>
	static bool assign(T& to, const T& value)
	{
		if (to == value)
			return false;
		to = value;
		return true;
	}

	static bool assignAim(rp::Aim& to, const rp::Aim& value)
	{
		const bool changed = to.panDegrees != value.panDegrees || to.tiltDegrees != value.tiltDegrees || to.rollDegrees != value.rollDegrees;
		to = value;
		return changed;
	}

	bool apply(rp::RigPlan& plan, const rp::NominalLayout& layout, const Selection& selection, GizmoMode mode, const math::Mat4d& manipulated)
	{
		const math::RigidTransformd pose = rigidPart(manipulated);
		const math::Vec3d& t = pose.translation();
		const bool moving = mode == GizmoMode::Translate;
		switch (selection.kind)
		{
			case Selected::Mount:
			{
				rp::Mount* mount = byId(plan.mounts, selection.id);
				if (!mount)
					return false;
				if (!moving)
					return assign(mount->yawDegrees, yawOf(pose.rotation()));
				if (auto* pole = std::get_if<rp::Pole>(&mount->kind))
					return assign(pole->floor, math::Vec2d{t.x, t.y}); // a pole stands on the floor
				return assign(std::get<rp::FixedPoint>(mount->kind).position, t);
			}
			case Selected::Camera:
			case Selected::Light:
			{
				math::Vec3d* offset = nullptr;
				rp::Aim* aim = nullptr;
				Uuid mount;
				if (selection.kind == Selected::Camera)
				{
					rp::PlacedCamera* camera = byId(plan.cameras, selection.id);
					if (!camera)
						return false;
					offset = &camera->offset;
					aim = &camera->aim;
					mount = camera->mount;
				}
				else
				{
					rp::PlacedLight* light = byId(plan.lights, selection.id);
					if (!light)
						return false;
					offset = &light->offset;
					aim = &light->aim;
					mount = light->mount;
				}
				const std::optional<math::RigidTransformd> planFromMount = mountPose(layout, mount);
				if (!planFromMount)
					return false;
				const math::RigidTransformd mountFromTarget = planFromMount->inverse() * pose;
				if (moving)
					return assign(*offset, mountFromTarget.translation());
				return assignAim(*aim, rp::aimOf(mountFromTarget.rotation()));
			}
			case Selected::Plane:
			{
				rp::Plane* plane = byId(plan.planes, selection.id);
				if (!plane)
					return false;
				return moving ? assign(plane->centre, t) : assignAim(plane->aim, rp::aimOf(pose.rotation()));
			}
			case Selected::Box:
			{
				rp::Box* box = byId(plan.boxes, selection.id);
				if (!box)
					return false;
				return moving ? assign(box->centre, t) : assign(box->yawDegrees, yawOf(pose.rotation()));
			}
			case Selected::Prop:
			{
				rp::Prop* prop = byId(plan.props, selection.id);
				if (!prop || prop->path)
					return false;
				return moving ? assign(prop->position, t) : assign(prop->yawDegrees, yawOf(pose.rotation()));
			}
			case Selected::Key:
			{
				rp::Path* path = pathOf(plan, selection.id);
				if (!path || selection.key >= path->keys.size())
					return false;
				rp::PathKey& key = path->keys[selection.key];
				return moving ? assign(key.position, t) : assignAim(key.aim, rp::aimOf(pose.rotation()));
			}
			case Selected::Room:
				if (!plan.room || !moving)
					return false; // a room does not turn
				return assign(plan.room->centre, math::Vec2d{t.x, t.y});
			case Selected::Nothing:
			case Selected::Plan:
			case Selected::Board:
			case Selected::Specification:
				return false;
		}
		return false;
	}

	GizmoMatrices gizmoMatrices(const lain::render::View& view)
	{
		// lain's camera frame (x right, y down, z forward) into OpenGL's (x right, y up, looking down -z).
		const math::Mat4d glFromCamera{1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
		const math::Mat4d viewMatrix = glFromCamera * view.worldFromCamera.inverse().matrix();

		// Into OpenGL's clip space so that, after ImGuizmo's ndc to pane mapping (x from -1 at the left edge,
		// y from +1 at the top), a camera-frame point lain projects to (u, v) lands at (u + 0.5, v + 0.5) of
		// the image's width w and height h. With the camera's depth z = -zgl:
		//   ndc.x = 2 (u + 0.5) / w - 1, so clip.x = (2 fx / w) xgl + (1 - 2 (cx + 0.5) / w) zgl;
		//   ndc.y = 1 - 2 (v + 0.5) / h, so clip.y = (2 fy / h) ygl + (2 (cy + 0.5) / h - 1) zgl;
		// and the depth is OpenGL's own, -1 at the near plane and 1 at the far.
		const lain::camera::Intrinsics& k = view.intrinsics;
		const double w = double(view.image.width);
		const double h = double(view.image.height);
		const double n = view.nearPlane;
		const double f = view.farPlane;
		math::Mat4d projection{0.0}; // GLM: projection[column][row]
		projection[0][0] = 2.0 * k.fx / w;
		projection[2][0] = 1.0 - 2.0 * (k.cx + 0.5) / w;
		projection[1][1] = 2.0 * k.fy / h;
		projection[2][1] = 2.0 * (k.cy + 0.5) / h - 1.0;
		projection[2][2] = -(f + n) / (f - n);
		projection[3][2] = -2.0 * f * n / (f - n);
		projection[2][3] = -1.0;

		GizmoMatrices out{};
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				out.view[std::size_t(c * 4 + r)] = float(viewMatrix[c][r]);
				out.projection[std::size_t(c * 4 + r)] = float(projection[c][r]);
			}
		}
		return out;
	}
} // namespace rigplan
