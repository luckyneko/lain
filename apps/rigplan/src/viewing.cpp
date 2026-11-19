#include "viewing.h"

#include <cstddef>
#include <cstdint>

namespace rigplan
{
	namespace math = lain::math;
	namespace rp = lain::rigplan;

	// A box growing to hold what it is given.
	struct GrowingBounds
	{
		std::optional<Bounds> bounds;

		void add(const math::Vec3d& point)
		{
			if (!bounds)
				bounds = Bounds{point, point};
			bounds->lower = math::min(bounds->lower, point);
			bounds->upper = math::max(bounds->upper, point);
		}

		void add(const lain::render::Scene& scene, const lain::render::Instance& instance)
		{
			if (instance.mesh >= scene.meshes.size() || !scene.meshes[instance.mesh])
				return;
			for (const math::Vec3f& p : scene.meshes[instance.mesh]->positions)
				add(math::Vec3d(instance.worldFromMesh * math::Vec4d(math::Vec3d(p), 1.0)));
		}

		void addPath(const rp::Path& path)
		{
			for (const rp::PathKey& key : path.keys)
				add(key.position);
		}
	};

	std::optional<Bounds> boundsOfEverything(const rp::RigPlan& plan, const rp::NominalLayout& layout, const rp::SceneResult& scene)
	{
		GrowingBounds grow;
		for (const lain::render::Instance& instance : scene.scene.instances)
			grow.add(scene.scene, instance);
		for (const rp::NominalLayout::Camera& camera : layout.cameras)
		{
			if (camera.planFromCamera)
				grow.add(camera.planFromCamera->translation());
		}
		for (const rp::NominalLayout::Light& light : layout.lights)
		{
			if (light.planFromLight)
				grow.add(light.planFromLight->translation());
		}
		for (const rp::NominalLayout::Mount& mount : layout.mounts)
		{
			if (mount.planFromMount)
				grow.add(mount.planFromMount->translation());
		}
		for (const rp::BoardMover& board : plan.boards)
			grow.addPath(board.path);
		for (const rp::Prop& prop : plan.props)
		{
			if (prop.path)
				grow.addPath(*prop.path);
		}
		return grow.bounds;
	}

	// Whether the scene instance drawing `owner` is part of what `selection` names.
	static bool owns(const Selection& selection, const rp::SceneOwner& owner)
	{
		switch (owner.object)
		{
			case rp::SceneObject::Room:
				return selection.kind == Selected::Room;
			case rp::SceneObject::Plane:
				return selection.kind == Selected::Plane && selection.id == owner.id;
			case rp::SceneObject::Box:
				return selection.kind == Selected::Box && selection.id == owner.id;
			case rp::SceneObject::Prop:
				return (selection.kind == Selected::Prop || selection.kind == Selected::Key) && selection.id == owner.id;
			case rp::SceneObject::Board:
				return (selection.kind == Selected::Board || selection.kind == Selected::Key) && selection.id == owner.id;
			case rp::SceneObject::Pole:
				return selection.kind == Selected::Mount && selection.id == owner.id;
		}
		return false;
	}

	std::optional<Bounds> boundsOfSelection(const rp::RigPlan& plan, const rp::NominalLayout& layout, const rp::SceneResult& scene,
											const Selection& selection)
	{
		if (selection.kind == Selected::Nothing || selection.kind == Selected::Plan || selection.kind == Selected::Specification)
			return boundsOfEverything(plan, layout, scene);

		GrowingBounds grow;
		for (std::size_t i = 0; i < scene.scene.instances.size() && i < scene.owners.size(); ++i)
		{
			if (owns(selection, scene.owners[i]))
				grow.add(scene.scene, scene.scene.instances[i]);
		}
		// What has no instance of its own: a camera, a light, a fixed point, a key, and what a mount carries.
		for (std::size_t i = 0; i < plan.cameras.size() && i < layout.cameras.size(); ++i)
		{
			const bool chosen = (selection.kind == Selected::Camera && selection.id == plan.cameras[i].id) ||
								(selection.kind == Selected::Mount && selection.id == plan.cameras[i].mount);
			if (chosen && layout.cameras[i].planFromCamera)
				grow.add(layout.cameras[i].planFromCamera->translation());
		}
		for (std::size_t i = 0; i < plan.lights.size() && i < layout.lights.size(); ++i)
		{
			const bool chosen = (selection.kind == Selected::Light && selection.id == plan.lights[i].id) ||
								(selection.kind == Selected::Mount && selection.id == plan.lights[i].mount);
			if (chosen && layout.lights[i].planFromLight)
				grow.add(layout.lights[i].planFromLight->translation());
		}
		for (const rp::NominalLayout::Mount& mount : layout.mounts)
		{
			if (selection.kind == Selected::Mount && selection.id == mount.id && mount.planFromMount)
				grow.add(mount.planFromMount->translation());
		}
		if (selection.kind == Selected::Key)
		{
			const rp::Path* path = pathOf(plan, selection.id);
			if (path && selection.key < path->keys.size())
				grow.add(path->keys[selection.key].position);
		}
		else if (selection.kind == Selected::Board || selection.kind == Selected::Prop)
		{
			if (const rp::Path* path = pathOf(plan, selection.id))
				grow.addPath(*path);
		}
		if (!grow.bounds)
			return std::nullopt;

		// Half a metre across at least, about its centre.
		Bounds& b = *grow.bounds;
		const math::Vec3d centre = (b.lower + b.upper) / 2.0;
		const math::Vec3d half = math::max((b.upper - b.lower) / 2.0, math::Vec3d{0.25});
		return Bounds{centre - half, centre + half};
	}

	std::vector<bool> cutawayMask(const rp::RigPlan& plan, const rp::SceneResult& scene, const math::Vec3d& eye)
	{
		std::vector<bool> hidden(scene.scene.instances.size(), false);
		if (!plan.room)
			return hidden;
		const math::Vec3d inside{plan.room->centre.x, plan.room->centre.y, plan.room->size.z / 2.0};
		for (std::size_t i = 0; i < scene.scene.instances.size() && i < scene.owners.size(); ++i)
		{
			if (scene.owners[i].object != rp::SceneObject::Room)
				continue;
			const lain::render::Instance& instance = scene.scene.instances[i];
			if (instance.mesh >= scene.scene.meshes.size() || !scene.scene.meshes[instance.mesh])
				continue;
			const lain::mesh::Mesh& mesh = *scene.scene.meshes[instance.mesh];
			if (mesh.indices.size() < 3)
				continue;
			// The surface's plane, from its first triangle; whichever way it winds, the room's inside and the
			// eye on opposite sides of it is the eye outside the surface.
			const auto at = [&](std::uint32_t index)
			{ return math::Vec3d(instance.worldFromMesh * math::Vec4d(math::Vec3d(mesh.positions[index]), 1.0)); };
			const math::Vec3d a = at(mesh.indices[0]);
			const math::Vec3d n = math::cross(at(mesh.indices[1]) - a, at(mesh.indices[2]) - a);
			hidden[i] = math::dot(eye - a, n) * math::dot(inside - a, n) < 0.0;
		}
		return hidden;
	}

	rp::SceneResult cutaway(const rp::RigPlan& plan, const rp::SceneResult& scene, const math::Vec3d& eye)
	{
		const std::vector<bool> hidden = cutawayMask(plan, scene, eye);
		rp::SceneResult shown = scene;
		shown.scene.instances.clear();
		shown.owners.clear();
		for (std::size_t i = 0; i < scene.scene.instances.size(); ++i)
		{
			if (hidden[i])
				continue;
			shown.scene.instances.push_back(scene.scene.instances[i]);
			if (i < scene.owners.size())
				shown.owners.push_back(scene.owners[i]);
		}
		return shown;
	}

	lain::render::Grid floorGrid(const rp::RigPlan& plan)
	{
		lain::render::Grid grid;
		grid.spacing = 0.1;
		grid.majorEvery = 10;
		if (plan.room && plan.room->size.x > 0.0 && plan.room->size.y > 0.0) // a room of no floor is check()'s to report
		{
			grid.origin = {plan.room->centre.x, plan.room->centre.y, 0.0};
			grid.extent = {plan.room->size.x / 2.0, plan.room->size.y / 2.0};
		}
		else
			grid.extent = {20.0, 20.0};
		return grid;
	}
} // namespace rigplan
