#include "picking.h"

#include "overlays.h" // kMarkerRadius: a marker is as big to click as it is drawn

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>

namespace rigplan
{
	namespace math = lain::math;
	namespace rp = lain::rigplan;

	Ray rayThrough(const lain::render::View& view, math::Vec2d pixel)
	{
		const lain::camera::Intrinsics& k = view.intrinsics;
		const math::Vec3d inCamera{(pixel.x - k.cx) / k.fx, (pixel.y - k.cy) / k.fy, 1.0};
		return {view.worldFromCamera.translation(), math::normalize(view.worldFromCamera.rotate(inCamera))};
	}

	std::optional<double> hitTriangle(const Ray& ray, const math::Vec3d& a, const math::Vec3d& b, const math::Vec3d& c)
	{
		// Moller-Trumbore, either face: a room is seen from inside and a plane from both sides. A hair of
		// slack on the edges, since a click on an edge two triangles share (a rectangle's diagonal, through
		// its centre) can otherwise fall between them by rounding and miss both.
		constexpr double kSlack = 1e-9;
		const math::Vec3d ab = b - a;
		const math::Vec3d ac = c - a;
		const math::Vec3d p = math::cross(ray.direction, ac);
		const double det = math::dot(ab, p);
		if (std::abs(det) < 1e-12)
			return std::nullopt; // edge on
		const double inverse = 1.0 / det;
		const math::Vec3d s = ray.origin - a;
		const double u = math::dot(s, p) * inverse;
		if (u < -kSlack || u > 1.0 + kSlack)
			return std::nullopt;
		const math::Vec3d q = math::cross(s, ab);
		const double v = math::dot(ray.direction, q) * inverse;
		if (v < -kSlack || u + v > 1.0 + kSlack)
			return std::nullopt;
		const double t = math::dot(ac, q) * inverse;
		return t > 0.0 ? std::optional<double>(t) : std::nullopt;
	}

	// The distance along `ray` to where it enters the box of half extents `half` about `pose`'s origin,
	// in its axes, or to where it leaves when it starts inside; none when it misses. Its bounds are not
	// `near` and `far`, which <windows.h> defines as empty macros.
	static std::optional<double> hitBox(const Ray& ray, const math::RigidTransformd& pose, const math::Vec3d& half)
	{
		const math::RigidTransformd boxFromPlan = pose.inverse();
		const math::Vec3d origin = boxFromPlan.apply(ray.origin);
		const math::Vec3d direction = boxFromPlan.rotate(ray.direction);
		double entry = -std::numeric_limits<double>::infinity();
		double exit = std::numeric_limits<double>::infinity();
		for (int axis = 0; axis < 3; ++axis)
		{
			if (std::abs(direction[axis]) < 1e-15)
			{
				if (std::abs(origin[axis]) > half[axis])
					return std::nullopt;
				continue;
			}
			double t0 = (-half[axis] - origin[axis]) / direction[axis];
			double t1 = (half[axis] - origin[axis]) / direction[axis];
			if (t0 > t1)
				std::swap(t0, t1);
			entry = std::max(entry, t0);
			exit = std::min(exit, t1);
		}
		if (entry > exit || exit <= 0.0)
			return std::nullopt;
		return entry > 0.0 ? entry : exit;
	}

	static std::optional<double> hitSphere(const Ray& ray, const math::Vec3d& centre, double radius)
	{
		const math::Vec3d toCentre = centre - ray.origin;
		const double along = math::dot(toCentre, ray.direction);
		const double miss = math::dot(toCentre, toCentre) - along * along;
		if (miss > radius * radius)
			return std::nullopt;
		const double half = std::sqrt(radius * radius - miss);
		const double t = along - half > 0.0 ? along - half : along + half;
		return t > 0.0 ? std::optional<double>(t) : std::nullopt;
	}

	// The nearest triangle of `instance` along `ray`.
	static std::optional<double> hitInstance(const Ray& ray, const lain::render::Scene& scene, const lain::render::Instance& instance)
	{
		if (instance.mesh >= scene.meshes.size() || !scene.meshes[instance.mesh])
			return std::nullopt;
		const lain::mesh::Mesh& mesh = *scene.meshes[instance.mesh];
		const auto placed = [&](std::uint32_t index)
		{ return math::Vec3d(instance.worldFromMesh * math::Vec4d(math::Vec3d(mesh.positions[index]), 1.0)); };
		std::optional<double> nearest;
		for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const std::optional<double> t = hitTriangle(ray, placed(mesh.indices[i]), placed(mesh.indices[i + 1]), placed(mesh.indices[i + 2]));
			if (t && (!nearest || *t < *nearest))
				nearest = t;
		}
		return nearest;
	}

	// The selection a scene instance stands for.
	static Selection ownerSelection(const rp::SceneOwner& owner)
	{
		switch (owner.object)
		{
			case rp::SceneObject::Room:
				return Selection::room();
			case rp::SceneObject::Plane:
				return Selection::object(Selected::Plane, owner.id);
			case rp::SceneObject::Box:
				return Selection::object(Selected::Box, owner.id);
			case rp::SceneObject::Prop:
				return Selection::object(Selected::Prop, owner.id);
			case rp::SceneObject::Board:
				return Selection::object(Selected::Board, owner.id);
			case rp::SceneObject::Pole:
				return Selection::object(Selected::Mount, owner.id);
		}
		return Selection::nothing();
	}

	// The nearest hit so far, and what it selects.
	struct Nearest
	{
		double t = std::numeric_limits<double>::infinity();
		Selection selection;

		void offer(const std::optional<double>& hit, const Selection& what)
		{
			if (hit && *hit < t)
			{
				t = *hit;
				selection = what;
			}
		}
	};

	static void offerPath(Nearest& nearest, const Ray& ray, const lain::core::Uuid& mover, const rp::Path& path)
	{
		for (std::size_t k = 0; k < path.keys.size(); ++k)
			nearest.offer(hitSphere(ray, path.keys[k].position, kMarkerRadius), Selection::keyOf(mover, k));
	}

	Selection pick(const rp::RigPlan& plan, const rp::NominalLayout& layout, const rp::SceneResult& scene, const Ray& ray)
	{
		Nearest nearest;
		Nearest room;
		for (std::size_t i = 0; i < scene.scene.instances.size() && i < scene.owners.size(); ++i)
		{
			const rp::SceneOwner& owner = scene.owners[i];
			const std::optional<double> hit = hitInstance(ray, scene.scene, scene.scene.instances[i]);
			(owner.object == rp::SceneObject::Room ? room : nearest).offer(hit, ownerSelection(owner));
		}

		for (std::size_t i = 0; i < plan.cameras.size() && i < layout.cameras.size(); ++i)
		{
			if (!layout.cameras[i].planFromCamera)
				continue;
			const rp::CameraSpecification* spec = rp::findCamera(plan.specifications, plan.cameras[i].camera);
			const rp::SensorSpecification* sensor = spec ? rp::findSensor(plan.specifications, spec->sensor) : nullptr;
			const math::Vec3d half = (sensor ? sensor->body.size : rp::SensorBody{}.size) / 2.0;
			nearest.offer(hitBox(ray, *layout.cameras[i].planFromCamera, half), Selection::object(Selected::Camera, plan.cameras[i].id));
		}
		for (std::size_t i = 0; i < plan.lights.size() && i < layout.lights.size(); ++i)
		{
			if (layout.lights[i].planFromLight)
				nearest.offer(hitSphere(ray, layout.lights[i].planFromLight->translation(), kMarkerRadius), Selection::object(Selected::Light, plan.lights[i].id));
		}
		for (std::size_t i = 0; i < plan.mounts.size() && i < layout.mounts.size(); ++i)
		{
			if (std::holds_alternative<rp::FixedPoint>(plan.mounts[i].kind) && layout.mounts[i].planFromMount)
				nearest.offer(hitSphere(ray, layout.mounts[i].planFromMount->translation(), kMarkerRadius), Selection::object(Selected::Mount, plan.mounts[i].id));
		}
		for (const rp::BoardMover& board : plan.boards)
			offerPath(nearest, ray, board.id, board.path);
		for (const rp::Prop& prop : plan.props)
		{
			if (prop.path)
				offerPath(nearest, ray, prop.id, *prop.path);
		}
		return nearest.selection.kind != Selected::Nothing ? nearest.selection : room.selection;
	}
} // namespace rigplan
