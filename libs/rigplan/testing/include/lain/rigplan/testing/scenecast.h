#pragma once

// A rig plan's render scene asked on the CPU what a camera's ray met: the still scene behind its movers
// (staticScene), which says whether a feature track is a point of the room or something a mover carried,
// or the whole scene at a frame (renderScene), which says what a mover hid. The geometry is the scene's
// own triangles, the ones the rasteriser drew, cast through lain::render's ray cast.

#include <lain/camera/projection.h>
#include <lain/math/types.h>
#include <lain/render/raycast.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/scene.h>

#include <cstddef>
#include <optional>

namespace lain::rigplan::testing
{
	// Where a ray met a render scene, and what was drawn there.
	struct SceneHit
	{
		math::Vec3d point{0.0, 0.0, 0.0}; // in the plan frame
		double distance = 0;			  // metres along the ray
		SceneOwner owner;
	};

	// The nearest instance of `scene` that `ray` meets, or none.
	inline std::optional<SceneHit> cast(const SceneResult& scene, const render::Ray& ray)
	{
		std::optional<SceneHit> nearest;
		for (std::size_t i = 0; i < scene.scene.instances.size() && i < scene.owners.size(); ++i)
		{
			const std::optional<double> t = render::hitInstance(ray, scene.scene, scene.scene.instances[i]);
			if (t && (!nearest || *t < nearest->distance))
				nearest = SceneHit{ray.origin + *t * ray.direction, *t, scene.owners[i]};
		}
		return nearest;
	}

	// The ray `seen` looks along at `pixel` ((0, 0) the centre of the top-left pixel), from its optical
	// centre, through its own model: the ray a capture rendered that pixel's centre with (ADR-0028). None
	// where the model has no ray.
	inline std::optional<render::Ray> rayThrough(const RealisedCamera& seen, const math::Vec2d& pixel)
	{
		const camera::Unprojection<double> ray = camera::unproject(seen.model, pixel.x, pixel.y);
		if (!ray.ok())
			return std::nullopt;
		const math::Vec3d direction = seen.planFromCamera.rotate(math::Vec3d{ray.x, ray.y, ray.z});
		return render::Ray{seen.planFromCamera.translation(), math::normalize(direction)};
	}

	// What `seen` saw of `scene` at `pixel`, or none.
	inline std::optional<SceneHit> cast(const SceneResult& scene, const RealisedCamera& seen, const math::Vec2d& pixel)
	{
		const std::optional<render::Ray> ray = rayThrough(seen, pixel);
		return ray ? cast(scene, *ray) : std::nullopt;
	}
} // namespace lain::rigplan::testing
