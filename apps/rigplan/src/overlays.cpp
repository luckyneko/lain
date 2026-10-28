#include "overlays.h"

#include <lain/camera/projection.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <variant>

namespace rigplan
{
	namespace math = lain::math;
	namespace rp = lain::rigplan;
	using lain::render::Segment;

	std::vector<math::Vec3d> frustumOutline(const lain::camera::CameraModel& model, double depth)
	{
		const double w = double(model.image().width);
		const double h = double(model.image().height);
		// The outer edge of the image: pixel (0, 0)'s centre is half a pixel in from it.
		const std::array<math::Vec2d, 4> corners{math::Vec2d{-0.5, -0.5}, math::Vec2d{w - 0.5, -0.5}, math::Vec2d{w - 0.5, h - 0.5},
												 math::Vec2d{-0.5, h - 0.5}};
		std::vector<math::Vec3d> outline;
		for (std::size_t side = 0; side < corners.size(); ++side)
		{
			const math::Vec2d& from = corners[side];
			const math::Vec2d& to = corners[(side + 1) % corners.size()];
			for (int i = 0; i < kFrustumSamples; ++i)
			{
				const math::Vec2d pixel = math::mix(from, to, double(i) / double(kFrustumSamples));
				const lain::camera::Unprojection<double> ray = lain::camera::unproject(model, pixel.x, pixel.y);
				if (ray.ok())
					outline.push_back(math::Vec3d{ray.x, ray.y, ray.z} * depth); // a unit ray, so `depth` along it
			}
		}
		return outline;
	}

	// Whether the camera `placed` is what `selection` picks out: itself, the mount it is on, or a
	// specification it is made from.
	static bool selected(const rp::RigPlan& plan, const rp::PlacedCamera& placed, const Selection& selection)
	{
		switch (selection.kind)
		{
			case Selected::Camera:
				return selection.id == placed.id;
			case Selected::Mount:
				return selection.id == placed.mount;
			case Selected::Specification:
			{
				if (selection.specification == SpecificationKind::Camera)
					return selection.name == placed.camera;
				const rp::CameraSpecification* spec = rp::findCamera(plan.specifications, placed.camera);
				if (!spec)
					return false;
				if (selection.specification == SpecificationKind::Sensor)
					return selection.name == spec->sensor;
				return selection.specification == SpecificationKind::Lens && selection.name == spec->lens;
			}
			default:
				return false;
		}
	}

	static bool selected(const rp::PlacedLight& placed, const Selection& selection)
	{
		switch (selection.kind)
		{
			case Selected::Light:
				return selection.id == placed.id;
			case Selected::Mount:
				return selection.id == placed.mount;
			case Selected::Specification:
				return selection.specification == SpecificationKind::Light && selection.name == placed.light;
			default:
				return false;
		}
	}

	// Whether a scene instance drawing `owner` is part of what `selection` picks out.
	static bool selected(const rp::SceneOwner& owner, const Selection& selection)
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
				return selection.kind == Selected::Prop && selection.id == owner.id;
			case rp::SceneObject::Board:
				return selection.kind == Selected::Board && selection.id == owner.id;
			case rp::SceneObject::Pole:
				return selection.kind == Selected::Mount && selection.id == owner.id;
		}
		return false;
	}

	static void line(std::vector<Segment>& out, const math::Vec3d& from, const math::Vec3d& to, const lain::image::ColorRGBf& colour)
	{
		out.push_back({from, to, colour});
	}

	// The twelve edges of the box whose corners are `corner(i)` for i in 0..7, bit 0 choosing x, bit 1 y
	// and bit 2 z.
	template <typename Corner>
	static void boxEdges(std::vector<Segment>& out, Corner corner, const lain::image::ColorRGBf& colour)
	{
		for (int i = 0; i < 8; ++i)
		{
			for (int bit : {1, 2, 4})
			{
				if (!(i & bit))
					line(out, corner(i), corner(i | bit), colour);
			}
		}
	}

	// A small octahedron round `centre`: a light's, a key's.
	static void marker(std::vector<Segment>& out, const math::Vec3d& centre, const lain::image::ColorRGBf& colour)
	{
		const double r = kMarkerRadius;
		const std::array<math::Vec3d, 6> tips{math::Vec3d{r, 0.0, 0.0}, math::Vec3d{-r, 0.0, 0.0}, math::Vec3d{0.0, r, 0.0},
											  math::Vec3d{0.0, -r, 0.0}, math::Vec3d{0.0, 0.0, r}, math::Vec3d{0.0, 0.0, -r}};
		for (std::size_t a = 0; a < 2; ++a)
		{
			for (std::size_t b = 2; b < 6; ++b)
				line(out, centre + tips[a], centre + tips[b], colour);
		}
		for (std::size_t b = 2; b < 4; ++b)
		{
			for (std::size_t c = 4; c < 6; ++c)
				line(out, centre + tips[b], centre + tips[c], colour);
		}
	}

	static void camera(std::vector<Segment>& out, const rp::RigPlan& plan, const rp::PlacedCamera& placed,
					   const rp::NominalLayout::Camera& laid, const OverlayOptions& options)
	{
		if (!laid.planFromCamera)
			return;
		const math::RigidTransformd& pose = *laid.planFromCamera;
		const lain::image::ColorRGBf colour = selected(plan, placed, options.selection) ? kSelectedColour : kCameraColour;

		const rp::CameraSpecification* spec = rp::findCamera(plan.specifications, placed.camera);
		const rp::SensorSpecification* sensor = spec ? rp::findSensor(plan.specifications, spec->sensor) : nullptr;
		const math::Vec3d half = (sensor ? sensor->body.size : rp::SensorBody{}.size) / 2.0;
		boxEdges(out, [&](int i)
				 { return pose.apply(math::Vec3d{i & 1 ? half.x : -half.x, i & 2 ? half.y : -half.y, i & 4 ? half.z : -half.z}); }, colour);

		if (!laid.model)
			return;
		const std::vector<math::Vec3d> outline = frustumOutline(*laid.model, options.frustumDepth);
		for (std::size_t i = 0; i < outline.size(); ++i)
			line(out, pose.apply(outline[i]), pose.apply(outline[(i + 1) % outline.size()]), colour);
		// The edges to the image's corners, when the model reaches them.
		const lain::camera::CameraModel& model = *laid.model;
		const double w = double(model.image().width);
		const double h = double(model.image().height);
		for (const math::Vec2d corner : {math::Vec2d{-0.5, -0.5}, math::Vec2d{w - 0.5, -0.5}, math::Vec2d{w - 0.5, h - 0.5}, math::Vec2d{-0.5, h - 0.5}})
		{
			const lain::camera::Unprojection<double> ray = lain::camera::unproject(model, corner.x, corner.y);
			if (ray.ok())
				line(out, pose.translation(), pose.apply(math::Vec3d{ray.x, ray.y, ray.z} * options.frustumDepth), colour);
		}
	}

	static void light(std::vector<Segment>& out, const rp::RigPlan& plan, const rp::PlacedLight& placed,
					  const rp::NominalLayout::Light& laid, const OverlayOptions& options)
	{
		if (!laid.planFromLight)
			return;
		const math::RigidTransformd& pose = *laid.planFromLight;
		const lain::image::ColorRGBf colour = selected(placed, options.selection) ? kSelectedColour : kLightColour;
		marker(out, pose.translation(), colour);

		const rp::LightSpecification* spec = rp::findLight(plan.specifications, placed.light);
		if (!spec || spec->kind != rp::LightKind::Spot)
			return;
		// The outer cone, out to the frustum depth: a ring and four lines to it. Held short of a half space.
		const double depth = options.frustumDepth;
		const double radius = depth * std::tan(math::radians(std::min(spec->outerConeDegrees, 85.0)));
		constexpr int kRing = 4 * kFrustumSamples;
		for (int i = 0; i < kRing; ++i)
		{
			const double a = 2.0 * math::pi<double>() * double(i) / kRing;
			const double b = 2.0 * math::pi<double>() * double(i + 1) / kRing;
			const math::Vec3d from{radius * std::cos(a), radius * std::sin(a), depth};
			const math::Vec3d to{radius * std::cos(b), radius * std::sin(b), depth};
			line(out, pose.apply(from), pose.apply(to), colour);
			if (i % kFrustumSamples == 0)
				line(out, pose.translation(), pose.apply(from), colour);
		}
	}

	// A mover's path, a polyline through its keys: exactly the path, since poseAt moves linearly in
	// position between keys. And a marker at each key.
	static void path(std::vector<Segment>& out, const lain::core::Uuid& mover, const rp::Path& path, const Selection& selection)
	{
		const bool moverSelected = (selection.kind == Selected::Board || selection.kind == Selected::Prop) && selection.id == mover;
		const lain::image::ColorRGBf colour = moverSelected ? kSelectedColour : kPathColour;
		for (std::size_t k = 0; k < path.keys.size(); ++k)
		{
			const bool keySelected = selection.kind == Selected::Key && selection.id == mover && selection.key == k;
			marker(out, path.keys[k].position, keySelected ? kSelectedColour : colour);
			if (k + 1 < path.keys.size())
				line(out, path.keys[k].position, path.keys[k + 1].position, colour);
		}
	}

	// The box a scene instance's mesh fills, in its own coordinates, placed where the instance puts it:
	// what a selected surface or prop is outlined by.
	static void outline(std::vector<Segment>& out, const lain::render::Scene& scene, const lain::render::Instance& instance)
	{
		if (instance.mesh >= scene.meshes.size() || !scene.meshes[instance.mesh] || scene.meshes[instance.mesh]->positions.empty())
			return;
		math::Vec3d lower{std::numeric_limits<double>::infinity()};
		math::Vec3d upper{-std::numeric_limits<double>::infinity()};
		for (const math::Vec3f& p : scene.meshes[instance.mesh]->positions)
		{
			lower = math::min(lower, math::Vec3d(p));
			upper = math::max(upper, math::Vec3d(p));
		}
		boxEdges(out, [&](int i)
				 {
					 const math::Vec4d local{i & 1 ? upper.x : lower.x, i & 2 ? upper.y : lower.y, i & 4 ? upper.z : lower.z, 1.0};
					 return math::Vec3d(instance.worldFromMesh * local); }, kSelectedColour);
	}

	std::vector<Segment> overlays(const rp::RigPlan& plan, const rp::NominalLayout& layout, const rp::SceneResult& scene,
								  const OverlayOptions& options)
	{
		std::vector<Segment> out;
		// The layout lists each in the plan's own order, one for one.
		for (std::size_t i = 0; i < plan.cameras.size() && i < layout.cameras.size(); ++i)
			camera(out, plan, plan.cameras[i], layout.cameras[i], options);
		for (std::size_t i = 0; i < plan.lights.size() && i < layout.lights.size(); ++i)
			light(out, plan, plan.lights[i], layout.lights[i], options);
		// A fixed point has no geometry of its own, so a cross marks it.
		for (std::size_t i = 0; i < plan.mounts.size() && i < layout.mounts.size(); ++i)
		{
			if (!std::holds_alternative<rp::FixedPoint>(plan.mounts[i].kind) || !layout.mounts[i].planFromMount)
				continue;
			const math::Vec3d at = layout.mounts[i].planFromMount->translation();
			const bool chosen = options.selection.kind == Selected::Mount && options.selection.id == plan.mounts[i].id;
			for (const math::Vec3d axis : {math::Vec3d{1.0, 0.0, 0.0}, math::Vec3d{0.0, 1.0, 0.0}, math::Vec3d{0.0, 0.0, 1.0}})
				line(out, at - axis * kMarkerRadius, at + axis * kMarkerRadius, chosen ? kSelectedColour : kMountColour);
		}
		for (const rp::BoardMover& board : plan.boards)
			path(out, board.id, board.path, options.selection);
		for (const rp::Prop& prop : plan.props)
		{
			if (prop.path)
				path(out, prop.id, *prop.path, options.selection);
		}
		for (std::size_t i = 0; i < scene.scene.instances.size() && i < scene.owners.size(); ++i)
		{
			if (selected(scene.owners[i], options.selection))
				outline(out, scene.scene, scene.scene.instances[i]);
		}
		return out;
	}
} // namespace rigplan
