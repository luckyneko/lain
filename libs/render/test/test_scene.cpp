// Unit: what makes a render scene undrawable, one problem per fault. No device.

#include "testscene.h"

#include <lain/render/scene.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>

using namespace lain;
using namespace lain::render;

static Scene usable()
{
	Scene scene;
	scene.meshes = {testing::square()};
	auto texture = std::make_shared<image::Image>(2, 2, image::PixelFormat::RGBA8, image::ColorSpace::sRGB);
	scene.textures = {texture};
	Material material;
	material.texture = 0;
	scene.materials = {material};
	scene.instances = {Instance{0, math::Mat4d{1.0}, {0}}};
	Light spot;
	spot.kind = LightKind::Spot;
	scene.lights = {Light{}, spot};
	return scene;
}

TEST_CASE("a usable scene has nothing wrong with it", "[render][scene]")
{
	CHECK(validate(usable()).empty());
	CHECK(validate(Scene{}).empty()); // nothing to draw is still drawable: the background
}

TEST_CASE("each fault in a scene is one problem", "[render][scene]")
{
	const double nan = std::numeric_limits<double>::quiet_NaN();

	SECTION("a mesh that is missing")
	{
		Scene scene = usable();
		scene.meshes[0] = nullptr;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a mesh that is not usable")
	{
		Scene scene = usable();
		mesh::Mesh broken = *scene.meshes[0];
		broken.indices.pop_back(); // not whole triangles, and the part now runs past the indices
		scene.meshes[0] = std::make_shared<const mesh::Mesh>(broken);
		CHECK(!validate(scene).empty());
	}
	SECTION("an instance naming a mesh the scene lacks")
	{
		Scene scene = usable();
		scene.instances[0].mesh = 3;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("an instance with a material too few for its mesh")
	{
		Scene scene = usable();
		scene.instances[0].materials.clear();
		CHECK(validate(scene).size() == 1);
	}
	SECTION("an instance naming a material the scene lacks")
	{
		Scene scene = usable();
		scene.instances[0].materials = {4};
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a transform that is not finite")
	{
		Scene scene = usable();
		scene.instances[0].worldFromMesh[3][0] = nan;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a texture that is missing")
	{
		Scene scene = usable();
		scene.textures[0] = nullptr;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a texture that does not say what it is encoded in")
	{
		Scene scene = usable();
		scene.textures[0] = std::make_shared<image::Image>(2, 2, image::PixelFormat::RGBA8);
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a material naming a texture the scene lacks")
	{
		Scene scene = usable();
		scene.materials[0].texture = 2;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a negative colour")
	{
		Scene scene = usable();
		scene.materials[0].diffuse.g = -0.1f;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a negative shininess")
	{
		Scene scene = usable();
		scene.materials[0].shininess = -1;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a light with a negative intensity")
	{
		Scene scene = usable();
		scene.lights[0].intensity = -2;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a spot with no direction")
	{
		Scene scene = usable();
		scene.lights[1].direction = {0.0, 0.0, 0.0};
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a spot whose inner cone is not inside its outer one")
	{
		Scene scene = usable();
		scene.lights[1].innerConeDegrees = 30;
		scene.lights[1].outerConeDegrees = 30;
		CHECK(validate(scene).size() == 1);
	}
	SECTION("a negative ambient light")
	{
		Scene scene = usable();
		scene.ambient.r = -1;
		CHECK(validate(scene).size() == 1);
	}
}
