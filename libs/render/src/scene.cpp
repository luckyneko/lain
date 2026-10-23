#include "lain/render/scene.h"

#include <lain/string/format.h>

#include <cmath>

namespace lain::render
{
	static bool finite(const image::ColorRGBf& c)
	{
		return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b);
	}

	static bool usableColour(const image::ColorRGBf& c)
	{
		return finite(c) && c.r >= 0 && c.g >= 0 && c.b >= 0;
	}

	static bool finite(const math::Vec3d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	static bool finite(const math::Mat4d& m)
	{
		for (math::length_t c = 0; c < 4; ++c)
		{
			for (math::length_t r = 0; r < 4; ++r)
			{
				if (!std::isfinite(m[c][r]))
					return false;
			}
		}
		return true;
	}

	std::vector<std::string> validate(const Scene& scene)
	{
		std::vector<std::string> problems;
		for (std::size_t m = 0; m < scene.meshes.size(); ++m)
		{
			if (!scene.meshes[m])
			{
				problems.push_back(string::format("mesh {} is missing", m));
				continue;
			}
			for (const std::string& problem : mesh::validate(*scene.meshes[m]))
				problems.push_back(string::format("mesh {}: {}", m, problem));
		}

		for (std::size_t t = 0; t < scene.textures.size(); ++t)
		{
			const std::shared_ptr<const image::Image>& texture = scene.textures[t];
			if (!texture || !texture->valid())
				problems.push_back(string::format("texture {} is missing or empty", t));
			else if (texture->colorSpace() == image::ColorSpace::Unspecified)
				problems.push_back(string::format("texture {} does not say what colour space its values are in", t));
		}

		for (std::size_t m = 0; m < scene.materials.size(); ++m)
		{
			const Material& material = scene.materials[m];
			if (!usableColour(material.diffuse) || !usableColour(material.specular))
				problems.push_back(string::format("material {} has a colour that is negative or not finite", m));
			if (!std::isfinite(material.shininess) || material.shininess < 0)
				problems.push_back(string::format("material {} has a shininess that is negative or not finite", m));
			if (material.texture && *material.texture >= scene.textures.size())
				problems.push_back(string::format("material {} names texture {} of {}", m, *material.texture, scene.textures.size()));
		}

		for (std::size_t i = 0; i < scene.instances.size(); ++i)
		{
			const Instance& instance = scene.instances[i];
			if (instance.mesh >= scene.meshes.size())
			{
				problems.push_back(string::format("instance {} names mesh {} of {}", i, instance.mesh, scene.meshes.size()));
				continue;
			}
			if (const std::shared_ptr<const mesh::Mesh>& mesh = scene.meshes[instance.mesh])
			{
				if (instance.materials.size() != mesh->materials.size())
					problems.push_back(string::format("instance {} gives {} materials for the {} of mesh {}", i,
													  instance.materials.size(), mesh->materials.size(), instance.mesh));
			}
			for (const std::size_t material : instance.materials)
			{
				if (material >= scene.materials.size())
				{
					problems.push_back(string::format("instance {} names material {} of {}", i, material, scene.materials.size()));
					break;
				}
			}
			if (!finite(instance.worldFromMesh))
				problems.push_back(string::format("instance {} has a transform that is not finite", i));
		}

		for (std::size_t l = 0; l < scene.lights.size(); ++l)
		{
			const Light& light = scene.lights[l];
			if (!finite(light.position))
				problems.push_back(string::format("light {} has a position that is not finite", l));
			if (!usableColour(light.colour) || !std::isfinite(light.intensity) || light.intensity < 0)
				problems.push_back(string::format("light {} has a colour or intensity that is negative or not finite", l));
			if (light.kind == LightKind::Spot)
			{
				if (!finite(light.direction) || math::length(light.direction) <= 0)
					problems.push_back(string::format("spot {} has no direction", l));
				if (!(light.innerConeDegrees >= 0 && light.innerConeDegrees < light.outerConeDegrees && light.outerConeDegrees <= 180))
					problems.push_back(string::format("spot {} has a cone of {} to {} degrees, not 0 <= inner < outer <= 180", l,
													  light.innerConeDegrees, light.outerConeDegrees));
			}
		}

		if (!usableColour(scene.ambient) || !usableColour(scene.background))
			problems.push_back("the ambient light or the background is negative or not finite");
		return problems;
	}
} // namespace lain::render
