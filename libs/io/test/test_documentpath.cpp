// ADR-0027 for one path: relative on disk inside a document's folder tree, absolute in memory.

#include <lain/io/documentpath.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// A folder that need not exist: the rule is lexical.
static const fs::path kFolder = fs::path("/projects/hall");

TEST_CASE("inside the document's folder tree a path is written relative, and reads back absolute", "[io][documentpath]")
{
	REQUIRE(lain::io::relativizePath("/projects/hall/meshes/pole.obj", kFolder) == "meshes/pole.obj");
	REQUIRE(lain::io::resolvePath("meshes/pole.obj", kFolder) == "/projects/hall/meshes/pole.obj");
	// One spelling of the folder, so a trailing separator or a "." changes nothing.
	REQUIRE(lain::io::relativizePath("/projects/hall/meshes/pole.obj", "/projects/hall/./") == "meshes/pole.obj");
}

TEST_CASE("outside the tree a path stays absolute, and ../ is never written", "[io][documentpath]")
{
	REQUIRE(lain::io::relativizePath("/footage/take1/cam0.mp4", kFolder) == "/footage/take1/cam0.mp4");
	REQUIRE(lain::io::relativizePath("/projects/other/wall.png", kFolder) == "/projects/other/wall.png");
	REQUIRE(lain::io::resolvePath("/footage/take1/cam0.mp4", kFolder) == "/footage/take1/cam0.mp4");
}

TEST_CASE("an empty path and a uri with a scheme are left as written", "[io][documentpath]")
{
	REQUIRE(lain::io::relativizePath("", kFolder).empty());
	REQUIRE(lain::io::resolvePath("", kFolder).empty());
	// Never through std::filesystem, which on libstdc++ would collapse the "//".
	REQUIRE(lain::io::relativizePath("s3://bucket/hall/pole.obj", kFolder) == "s3://bucket/hall/pole.obj");
	REQUIRE(lain::io::resolvePath("s3://bucket/hall/pole.obj", kFolder) == "s3://bucket/hall/pole.obj");
}

TEST_CASE("a relative path in memory means the working directory", "[io][documentpath]")
{
	// Written as it is, a load would read it against the document instead: a different file.
	const std::string written = lain::io::relativizePath("pole.obj", kFolder);
	REQUIRE(fs::path(written).is_absolute());
	REQUIRE(written == (fs::current_path() / "pole.obj").lexically_normal().generic_string());
}
