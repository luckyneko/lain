// The OBJ reader, through io::mesh's own load(): what an OBJ and its MTL become.

#include <lain/io/mesh/load.h>
#include <lain/io/mesh/obj/register.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static void write(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

static std::optional<lain::mesh::Mesh> loaded(const fs::path& path)
{
	lain::io::mesh::obj::registerCodec();
	return lain::io::mesh::load(lain::core::Uri::fromPath(path));
}

// A unit cube: 8 positions, 4 texture coordinates, 6 face normals, 6 quad faces, the first three red
// and the last three blue.
static const char* kCube = R"(mtllib cube.mtl
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 0 0 1
v 1 0 1
v 1 1 1
v 0 1 1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 -1
vn 0 0 1
vn 0 -1 0
vn 0 1 0
vn -1 0 0
vn 1 0 0
usemtl red
f 1/1/1 4/4/1 3/3/1 2/2/1
f 5/1/2 6/2/2 7/3/2 8/4/2
f 1/1/3 2/2/3 6/3/3 5/4/3
usemtl blue
f 4/1/4 8/4/4 7/3/4 3/2/4
f 1/1/5 5/2/5 8/3/5 4/4/5
f 2/1/6 3/2/6 7/3/6 6/4/6
)";

static const char* kCubeMaterials = R"(newmtl red
Kd 1 0 0
map_Kd textures\red.png
newmtl blue
Kd 0 0 1
Ks 0.5 0.5 0.5
Ns 32
)";

TEST_CASE("an OBJ and its MTL become a mesh, its parts by material", "[io][mesh][obj]")
{
	const fs::path dir = lain::testing::scratchPath("cube");
	write(dir / "cube.obj", kCube);
	write(dir / "cube.mtl", kCubeMaterials);
	const std::optional<lain::mesh::Mesh> mesh = loaded(dir / "cube.obj");
	REQUIRE(mesh.has_value());

	// Six quads triangulated, and one vertex per distinct corner: each face has its own normal, so the
	// cube's 8 corners are 24 vertices.
	REQUIRE(mesh->indices.size() == 36);
	REQUIRE(mesh->positions.size() == 24);
	REQUIRE(mesh->normals.size() == 24);
	REQUIRE(mesh->uvs.size() == 24);

	REQUIRE(mesh->materials.size() == 2);
	REQUIRE(mesh->materials[0].name == "red");
	REQUIRE(mesh->materials[0].diffuse == lain::image::ColorRGBf{1.0f, 0.0f, 0.0f});
	// map_Kd resolved against the OBJ's folder, a Windows separator read as one, held absolute.
	REQUIRE(mesh->materials[0].diffuseTexture ==
			lain::core::Uri::fromPath((dir / "textures" / "red.png").lexically_normal()));
	REQUIRE(mesh->materials[1].specular == lain::image::ColorRGBf{0.5f, 0.5f, 0.5f});
	REQUIRE(mesh->materials[1].shininess == 32.0f);
	REQUIRE_FALSE(mesh->materials[1].diffuseTexture.has_value());

	REQUIRE(mesh->parts.size() == 2);
	REQUIRE(mesh->parts[0].material == 0);
	REQUIRE(mesh->parts[0].indexCount == 18);
	REQUIRE(mesh->parts[1].material == 1);
	REQUIRE(mesh->parts[1].firstIndex == 18);
}

TEST_CASE("an OBJ without normals or texture coordinates shares its corners by position", "[io][mesh][obj]")
{
	const fs::path dir = lain::testing::scratchPath("bare");
	write(dir / "quad.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n");
	const auto mesh = loaded(dir / "quad.obj");
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->positions.size() == 4);
	REQUIRE(mesh->indices.size() == 6);
	REQUIRE(mesh->normals.empty());
	REQUIRE(mesh->uvs.empty());
	// No material named, so one plain default holds every face.
	REQUIRE(mesh->materials.size() == 1);
	REQUIRE(mesh->materials[0].name == "default");
	REQUIRE(mesh->parts.size() == 1);
}

TEST_CASE("normals are all or none; a corner without a texture coordinate gets (0, 0)", "[io][mesh][obj]")
{
	const fs::path dir = lain::testing::scratchPath("partial");
	// The first triangle has normals and uvs, the second neither.
	write(dir / "two.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nvt 0.5 0.5\nvn 0 0 1\nf 1/1/1 2/1/1 3/1/1\nf 2 4 3\n");
	const auto mesh = loaded(dir / "two.obj");
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->normals.empty()); // half the corners had none, so none
	REQUIRE(mesh->uvs.size() == mesh->positions.size());
	REQUIRE(mesh->uvs.front() == lain::math::Vec2f{0.5f, 0.5f});
	REQUIRE(mesh->uvs.back() == lain::math::Vec2f{0.0f, 0.0f});
}

TEST_CASE("an OBJ whose MTL is missing still shows its shape, in the default material", "[io][mesh][obj]")
{
	const fs::path dir = lain::testing::scratchPath("lost");
	write(dir / "cube.obj", kCube); // names cube.mtl, which is not there
	const auto mesh = loaded(dir / "cube.obj");
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->materials.size() == 1);
	REQUIRE(mesh->materials[0].name == "default");
	REQUIRE(mesh->indices.size() == 36);
}

TEST_CASE("what is not an OBJ, or names a vertex it does not have, is refused", "[io][mesh][obj]")
{
	const fs::path dir = lain::testing::scratchPath("bad");
	write(dir / "words.obj", "this is not a mesh\n");
	write(dir / "stray.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n");
	REQUIRE_FALSE(loaded(dir / "words.obj").has_value());
	REQUIRE_FALSE(loaded(dir / "stray.obj").has_value());
}

TEST_CASE("an OBJ sequence is a clip", "[io][mesh][obj][clip]")
{
	lain::io::mesh::obj::registerCodec();
	const fs::path dir = lain::testing::scratchPath("walk");
	write(dir / "walk.0001.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
	write(dir / "walk.0002.obj", "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n");
	const auto clip = lain::io::mesh::loadClip(lain::core::Uri::fromPath(dir / "walk.<frame:04>.obj"), {12, 1});
	REQUIRE(clip.has_value());
	REQUIRE(clip->frames.size() == 2);
	REQUIRE(clip->frames[1].positions[2] == lain::math::Vec3f{0.0f, 2.0f, 0.0f});
}
