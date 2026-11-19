// numberedFiles: a numbered run of files, found by a pattern naming frameKey and ordered by number.

#include <lain/io/framepattern.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void touch(const fs::path& path)
{
	std::ofstream(path) << "x";
}

static std::vector<std::string> names(const std::vector<std::string>& paths)
{
	std::vector<std::string> out;
	for (const std::string& p : paths)
		out.push_back(fs::path(p).filename().string());
	return out;
}

TEST_CASE("a numbered run is ordered by its numbers, padded or not", "[io][framepattern]")
{
	const fs::path dir = lain::testing::scratchPath("run");
	fs::create_directories(dir);
	for (const char* name : {"walk.10.obj", "walk.0002.obj", "walk.7.obj", "walk.x.obj", "walk.3.mtl", "stand.1.obj"})
		touch(dir / name);
	fs::create_directories(dir / "walk.5.obj"); // a folder, not a file

	// Lexicographic order would put 10 before 2; a non-number, another extension, another name and a
	// folder are not frames of the run.
	REQUIRE(names(lain::io::numberedFiles(dir / "walk.<frame:04>.obj")) ==
			std::vector<std::string>{"walk.0002.obj", "walk.7.obj", "walk.10.obj"});
}

TEST_CASE("a pattern in a folder that cannot be read matches nothing", "[io][framepattern]")
{
	REQUIRE(lain::io::numberedFiles(lain::testing::scratchPath("absent") / "walk.<frame>.obj").empty());
}
