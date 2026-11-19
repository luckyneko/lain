#pragma once

#include <lain/image/color.h>
#include <lain/image/image.h>
#include <lain/math/types.h>
#include <lain/mesh/mesh.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lain::render
{
	// What a renderer draws (ADR-0028): meshes placed in a world, the materials their surfaces show,
	// and the lights that light them. It knows nothing of where a scene came from; a rig plan's
	// realisation at one frame is turned into one, and so could anything else that wants a picture.
	//
	// World coordinates are metres, in whatever axes the scene's author chose: a view states where
	// its camera is in the same axes, so the renderer needs no convention of its own. Colours and
	// light are linear RGB.

	// How a surface reflects light: Lambert's diffuse, its colour multiplied by a texture's, plus
	// Blinn-Phong's specular highlight (MTL's model), black by default. Lit from either side, since a
	// plane is seen from both and a room from inside.
	struct Material
	{
		image::ColorRGBf diffuse{0.8f, 0.8f, 0.8f}; // reflectance, multiplied by the texture's
		// An index into Scene::textures, sampled at the mesh's texture coordinates (OBJ's: v up, so v =
		// 1 is the picture's top row) and repeated outside [0, 1]. Without one the surface is its colour.
		std::optional<std::size_t> texture;
		image::ColorRGBf specular{0.0f, 0.0f, 0.0f};
		float shininess = 0; // Blinn-Phong's exponent: larger is a smaller, sharper highlight, and 0 is none
	};

	// One mesh placed in the world. A mesh's own materials (what its file states) are replaced, one
	// for one, by the scene's: `materials[i]` is what the mesh's material `i` is drawn with.
	struct Instance
	{
		std::size_t mesh = 0;				// an index into Scene::meshes
		math::Mat4d worldFromMesh{1.0};		// a point (x, y, z, 1) of the mesh into the world; need not be rigid
		std::vector<std::size_t> materials; // indices into Scene::materials, one per material of the mesh
	};

	enum class LightKind
	{
		Point, // shines every way
		Spot,  // shines into a cone about its direction
	};

	// A light at a point. Its light falls with the square of the distance: a surface facing it 1 m away
	// is lit by `colour` times `intensity`, in the renderer's own scale until lights gain physical
	// units. Nothing casts a shadow yet (WORK.md M15, *Not in this milestone*).
	struct Light
	{
		LightKind kind = LightKind::Point;
		math::Vec3d position{0.0, 0.0, 0.0};
		math::Vec3d direction{0.0, 0.0, 1.0}; // a spot's axis; need not be unit length, but not zero
		image::ColorRGBf colour{1.0f, 1.0f, 1.0f};
		double intensity = 1;
		// A spot's cone: full intensity within the inner angle of its axis, falling smoothly to none at
		// the outer one. Unused by a point light.
		double innerConeDegrees = 20;
		double outerConeDegrees = 30;
	};

	// Meshes and textures are held shared and immutable, since one loaded asset backs every frame's
	// scene and a mesh clip's frames are swapped in without a copy.
	struct Scene
	{
		std::vector<std::shared_ptr<const mesh::Mesh>> meshes;
		// Each tagged with the colour space its values are encoded in, sRGB for most pictures. Only its
		// colour channels are read: surfaces are opaque.
		std::vector<std::shared_ptr<const image::Image>> textures;
		std::vector<Material> materials;
		std::vector<Instance> instances;
		std::vector<Light> lights;
		image::ColorRGBf ambient{0.0f, 0.0f, 0.0f};	   // light every surface reflects, whatever lights it
		image::ColorRGBf background{0.0f, 0.0f, 0.0f}; // what a pixel no surface covers shows
	};

	// Why `scene` cannot be drawn: a mesh that is missing or not usable (mesh::validate), an instance
	// naming a mesh or material the scene lacks or not one material per material of its mesh, a
	// transform that is not finite, a texture missing or not tagged with its colour space, a colour
	// or intensity that is negative or not finite, a spot with no direction or a cone that is not
	// 0 <= inner < outer <= 180 degrees. Empty when it can be drawn.
	std::vector<std::string> validate(const Scene& scene);
} // namespace lain::render
