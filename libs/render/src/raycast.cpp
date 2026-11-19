#include "lain/render/raycast.h"

#include <cmath>
#include <cstdint>

namespace lain::render
{
	std::optional<double> hitTriangle(const Ray& ray, const math::Vec3d& a, const math::Vec3d& b, const math::Vec3d& c)
	{
		// Moller-Trumbore, either face.
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

	std::optional<double> hitInstance(const Ray& ray, const Scene& scene, const Instance& instance)
	{
		if (instance.mesh >= scene.meshes.size() || !scene.meshes[instance.mesh])
			return std::nullopt;
		const mesh::Mesh& mesh = *scene.meshes[instance.mesh];
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
} // namespace lain::render
