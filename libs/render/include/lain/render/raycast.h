#pragma once

#include <lain/math/types.h>
#include <lain/render/scene.h>

#include <optional>

namespace lain::render
{
	// A render scene asked on the CPU what a ray meets: an editor's click, or a test's question of what
	// the rasteriser drew at a pixel. Brute force over every triangle, which is what a scene of a room,
	// a few boxes and a few props costs nothing to ask; a scene of large meshes would want a bounding
	// volume hierarchy first.

	// A ray, from `origin` along the unit `direction`, in the frame of the scene it is cast into.
	struct Ray
	{
		math::Vec3d origin{0.0, 0.0, 0.0};
		math::Vec3d direction{0.0, 0.0, 1.0};
	};

	// The distance along `ray` to where it meets the triangle (a, b, c), from either side (a room is seen
	// from inside, a plane from both sides, and the renderer draws both faces), or none: one it meets
	// behind its origin or edge on is none. A hair of slack on the edges, so a ray along an edge two
	// triangles share (a rectangle's diagonal, through its centre) cannot fall between them by rounding
	// and miss both.
	std::optional<double> hitTriangle(const Ray& ray, const math::Vec3d& a, const math::Vec3d& b, const math::Vec3d& c);

	// The distance along `ray` to the nearest triangle of `instance` of `scene`, each placed by the
	// instance's worldFromMesh, or none. An instance naming a mesh the scene lacks meets nothing.
	std::optional<double> hitInstance(const Ray& ray, const Scene& scene, const Instance& instance);
} // namespace lain::render
