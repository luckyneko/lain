// Tests for lain::app's licence notices — what --licenses prints. Every dependency compiled or
// linked into the build carries a notice, and nothing that was not built does (ADR-0015, amended
// 2026-10-23). The two halves are independent: what was built comes from the configuration
// (LAIN_BUILT_*, asked of the plugin targets in CMakeLists.txt), what is named comes from the
// notices the dependencies registered (cmake/lainNotices.cmake).

#include "lain/app/notices.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace lain;

// Each notice's header line, the only lines that start in the first column: a notice's body, its
// licence text included, is indented under it. So a header is that dependency's own notice, never a
// mention of it in another's (OpenCV's names the zlib it compiles in).
static std::vector<std::string_view> headers(std::string_view notices)
{
	std::vector<std::string_view> found;
	while (!notices.empty())
	{
		const std::size_t end = notices.find('\n');
		const std::string_view line = notices.substr(0, end);
		if (!line.empty() && line.front() != ' ')
			found.push_back(line);
		if (end == std::string_view::npos)
			break;
		notices.remove_prefix(end + 1);
	}
	return found;
}

// A header names a dependency when it starts with the name and a space: "zlib 1.3.1 — Zlib".
static bool names(std::string_view header, std::string_view dependency)
{
	return header.size() > dependency.size() && header.substr(0, dependency.size()) == dependency &&
		   header[dependency.size()] == ' ';
}

// Not "--licenses names ...": ctest runs each case by passing its name to the executable, and a
// name that starts with "--" is read as an option.
TEST_CASE("the notices name each dependency this build contains, and only those", "[app]")
{
	struct Expected
	{
		std::string_view name;
		bool built;
	};
	const Expected expected[] = {
		// Every lain::app build: lain, its submodules, the libraries every lain lib builds on, the
		// Vulkan stack archimedes fetches, and the app stack.
		{"lain", true},
		{"archimedes", true},
		{"multi", true},
		{"GLM", true},
		{"{fmt}", true},
		{"spdlog", true},
		{"magic_enum", true},
		{"Vulkan-Headers", true},
		{"Vulkan-Loader", true},
		{"GLFW", true},
		{"CLI11", true},
		{"Dear ImGui", true},
		{"imnodes", true},
		{"portable-file-dialogs", true},
		// Where archimedes vendors a driver (macOS).
		{"MoltenVK", LAIN_BUILT_MOLTENVK != 0},
		// Each with the plugin that brings it in.
		{"nlohmann/json", LAIN_BUILT_JSON != 0},
		{"stb", LAIN_BUILT_JPEG != 0},
		{"libpng", LAIN_BUILT_PNG != 0},
		{"zlib", LAIN_BUILT_PNG != 0 || LAIN_BUILT_TIFF != 0},
		{"libtiff", LAIN_BUILT_TIFF != 0},
		{"tinyobjloader", LAIN_BUILT_OBJ != 0},
		{"FFmpeg", LAIN_BUILT_FFMPEG != 0},
		{"OpenCV", LAIN_BUILT_OPENCV != 0},
		{"Eigen", LAIN_BUILT_CERES != 0},
		{"Ceres Solver", LAIN_BUILT_CERES != 0},
	};

	const std::vector<std::string_view> printed = headers(app::thirdPartyNotices());

	SECTION("a built dependency has a notice, and one that was not built has none")
	{
		for (const Expected& dependency : expected)
		{
			INFO("dependency: " << dependency.name);
			int count = 0;
			for (std::string_view header : printed)
				count += names(header, dependency.name) ? 1 : 0;
			CHECK(count == (dependency.built ? 1 : 0));
		}
	}
	SECTION("every notice is one this table expects")
	{
		// A dependency that registers a notice must also be stated here, with its build condition,
		// or the first half could not check it.
		for (std::string_view header : printed)
		{
			INFO("notice: " << header);
			bool known = false;
			for (const Expected& dependency : expected)
				known = known || names(header, dependency.name);
			CHECK(known);
		}
	}
	SECTION("lain's own notice comes first")
	{
		REQUIRE_FALSE(printed.empty());
		CHECK(names(printed.front(), "lain"));
	}
}

TEST_CASE("a notice carries the licence file's own text", "[app]")
{
	// lain's LICENSE stands for every notice registered through lain_register_notice: each of its
	// lines is in --licenses, indented under the header, so the text is the file's and not a stub.
	std::ifstream file(LAIN_LICENSE_FILE);
	REQUIRE(file);
	const std::string notices(app::thirdPartyNotices());
	std::string line;
	int lines = 0;
	while (std::getline(file, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		INFO("licence line: " << line);
		CHECK(notices.find("\n  " + line + "\n") != std::string::npos);
		++lines;
	}
	CHECK(lines > 5); // MIT's copyright, permission and warranty paragraphs, at least
}
