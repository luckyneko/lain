// The io::mesh facade, through a small test format, since the seam holds no parser. A ".tri" file is one
// line: the name of a material file beside it, and a texture path to name in it.

#include <lain/io/mesh/load.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// "<material file> <texture path>": reads the material file through the folder (its first word is the
// material's name), and names the texture, as an OBJ's MTL does. "broken" makes a mesh that is not one.
class TriReader : public lain::io::mesh::MeshReader
{
public:
	std::optional<lain::mesh::Mesh> decode(const lain::memory::Buffer& bytes,
										   const lain::io::mesh::MeshFolder& folder) const override
	{
		std::istringstream in(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		std::string materialFile, texture;
		in >> materialFile >> texture;
		const std::optional<lain::memory::Buffer> material = folder.read(materialFile);
		if (!material)
			return std::nullopt;

		lain::mesh::Mesh mesh;
		mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
		mesh.indices = {0, 1, 2};
		std::istringstream materialText(std::string(reinterpret_cast<const char*>(material->data()), material->size()));
		lain::mesh::Material m;
		materialText >> m.name;
		m.diffuseTexture = folder.locate(texture);
		mesh.materials = {m};
		mesh.parts = {{0, 0, materialFile == "broken" ? 2u : 3u}};
		return mesh;
	}
};

static void write(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

static void registerTri()
{
	lain::io::mesh::readerRegistry().registerType<TriReader>("tri");
}

TEST_CASE("a mesh loads through the reader its extension names, reading what it names beside it", "[io][mesh]")
{
	registerTri();
	const fs::path dir = lain::testing::scratchPath("tri");
	write(dir / "box.tri", "box.mat textures\\wood.png");
	write(dir / "box.mat", "oak");

	const std::optional<lain::mesh::Mesh> mesh = lain::io::mesh::load(lain::core::Uri::fromPath(dir / "box.tri"));
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->materials[0].name == "oak");
	// A texture is located beside the mesh, a Windows separator read as one, and held absolute.
	REQUIRE(mesh->materials[0].diffuseTexture == lain::core::Uri::fromPath((dir / "textures" / "wood.png").lexically_normal()));
}

TEST_CASE("a mesh that does not load, or is not usable, is refused", "[io][mesh]")
{
	registerTri();
	const fs::path dir = lain::testing::scratchPath("tri");
	write(dir / "lonely.tri", "missing.mat wood.png"); // its material file is not there
	write(dir / "broken.tri", "broken wood.png");	   // the reader makes a part of two indices
	write(dir / "broken", "x");
	write(dir / "box.unknown", "box.mat wood.png");

	REQUIRE_FALSE(lain::io::mesh::load(lain::core::Uri::fromPath(dir / "lonely.tri")).has_value());
	REQUIRE_FALSE(lain::io::mesh::load(lain::core::Uri::fromPath(dir / "broken.tri")).has_value());
	REQUIRE_FALSE(lain::io::mesh::load(lain::core::Uri::fromPath(dir / "box.unknown")).has_value());
	REQUIRE_FALSE(lain::io::mesh::load(lain::core::Uri::fromPath(dir / "absent.tri")).has_value());
}

TEST_CASE("a mesh decoded from memory has no folder to read from", "[io][mesh]")
{
	registerTri();
	const std::string text = "box.mat wood.png";
	lain::memory::Buffer bytes(text.size());
	std::copy(text.begin(), text.end(), reinterpret_cast<char*>(bytes.data()));
	REQUIRE_FALSE(lain::io::mesh::decode("tri", bytes, lain::io::mesh::NoFolder{}).has_value());
}

TEST_CASE("a clip is a numbered run of meshes, in the order of their numbers", "[io][mesh][clip]")
{
	registerTri();
	const fs::path dir = lain::testing::scratchPath("walk");
	for (const auto& [number, material] : std::vector<std::pair<std::string, std::string>>{{"10", "c"}, {"2", "b"}, {"1", "a"}})
	{
		write(dir / ("walk." + number + ".tri"), material + ".mat skin.png");
		write(dir / (material + ".mat"), material);
	}

	const std::optional<lain::mesh::MeshClip> clip = lain::io::mesh::loadClip(lain::core::Uri::fromPath(dir / "walk.<frame>.tri"), {24, 1});
	REQUIRE(clip.has_value());
	REQUIRE(clip->frames.size() == 3);
	REQUIRE(clip->frames[0].materials[0].name == "a");
	REQUIRE(clip->frames[1].materials[0].name == "b");
	REQUIRE(clip->frames[2].materials[0].name == "c"); // 10 after 2, not before it
	REQUIRE(clip->rate == lain::media::FrameRate{24, 1});

	SECTION("a frame that does not load refuses the clip, rather than letting it jump")
	{
		write(dir / "walk.3.tri", "missing.mat skin.png");
		REQUIRE_FALSE(lain::io::mesh::loadClip(lain::core::Uri::fromPath(dir / "walk.<frame>.tri")).has_value());
	}
	SECTION("a pattern nothing matches is no clip")
	{
		REQUIRE_FALSE(lain::io::mesh::loadClip(lain::core::Uri::fromPath(dir / "run.<frame>.tri")).has_value());
	}
}
